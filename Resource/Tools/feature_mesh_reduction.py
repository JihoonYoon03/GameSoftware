"""Error-ranked edge retopology, preserving features, skin domains and ports.

This is not a Blender Decimate ratio pass. Candidate collapses stay on existing
edges, retain material regions/components, reject flipped faces and preserve
bounding/contact planes. A draft is rejected if its measured surface error or
remaining detail exceeds the delivery constraints.
"""
import collections,heapq,math
import numpy as np

class FeatureReducer:
    def __init__(self,points,triangles,materials,weights,locked=None,features=None,stations=None):
        self.p=np.array(points,dtype=np.float64);self.f=np.array(triangles,dtype=np.int32)
        self.mat=list(materials);self.weights=weights;self.alive=np.ones(len(self.f),dtype=bool)
        self.features=list(features) if features is not None else None
        self.stations=list(stations) if stations is not None else None
        self.va=np.ones(len(self.p),dtype=bool);self.vf=[set() for v in self.p]
        self.version=np.zeros(len(self.p),dtype=np.int64);self.q=np.zeros((len(self.p),4,4))
        self.fixed=set(locked or []);self.heap=[];self.serial=0;self.rejected=collections.Counter()
        self.lo=self.p.min(axis=0);self.hi=self.p.max(axis=0);diag=np.linalg.norm(self.hi-self.lo)
        eps=max(1e-8,diag*1e-8)
        self.bounds=[tuple((-1 if abs(p[k]-self.lo[k])<eps else 1 if abs(p[k]-self.hi[k])<eps else 0) if self.hi[k]-self.lo[k]>eps else 2 for k in range(3)) for p in self.p]
        for i,face in enumerate(self.f):
            for v in face:self.vf[v].add(i)
            a,b,c=self.p[face];normal=np.cross(b-a,c-a);length=np.linalg.norm(normal)
            if length<1e-16:continue
            normal/=length;plane=np.r_[normal,-np.dot(normal,a)]
            quad=np.outer(plane,plane)*max(length,diag*diag*1e-8)
            for v in face:self.q[v]+=quad
        self.component=np.full(len(self.f),-1,dtype=np.int32);self.comp_count=[];self.comp_min=[]
        for start in range(len(self.f)):
            if self.component[start]>=0:continue
            ci=len(self.comp_count);todo=[start];faces=set()
            while todo:
                i=todo.pop()
                if i in faces:continue
                faces.add(i);self.component[i]=ci
                for v in self.f[i]:
                    todo.extend(j for j in self.vf[v] if j not in faces)
            ids=list({v for i in faces for v in self.f[i]});coords=self.p[ids]
            rank=np.linalg.matrix_rank(coords-coords.mean(axis=0),tol=max(1e-10,diag*1e-8))
            self.comp_count.append(len(faces));self.comp_min.append(4 if rank==3 else min(2,len(faces)))
        self.regions=collections.Counter((self.component[i],self.mat[i]) for i in range(len(self.f)))
        self.feature_bounds={}
        if self.features is not None:
            members=collections.defaultdict(set)
            for i,f in enumerate(self.f):members[self.features[i]].update(f)
            for feature,ids in members.items():
                pp=self.p[list(ids)];self.feature_bounds[feature]=(pp.min(axis=0),pp.max(axis=0))
        edges={tuple(sorted((int(a),int(b)))) for f in self.f for a,b in zip(f,np.roll(f,-1))}
        for u,v in edges:self.push(u,v)

    def neighbors(self,u):return {int(v) for i in self.vf[u] for v in self.f[i] if v!=u}
    def candidate(self,u,v):
        if not self.va[u] or not self.va[v] or self.weights[u]!=self.weights[v]:return None
        if self.stations is not None and self.stations[u] is not None and self.stations[v] is not None and self.stations[u]!=self.stations[v]:return None
        if {self.mat[i] for i in self.vf[u]}!={self.mat[i] for i in self.vf[v]}:return None
        if self.features is not None and {self.features[i] for i in self.vf[u]}!={self.features[i] for i in self.vf[v]}:return None
        common=self.vf[u]&self.vf[v]
        if not common:return None
        if len(common)>2:return None
        if len(self.neighbors(u)&self.neighbors(v))!=len(common):return None
        removed=collections.Counter(self.component[i] for i in common)
        if any(self.comp_count[c]-n<self.comp_min[c] for c,n in removed.items()):return None
        removed_regions=collections.Counter((self.component[i],self.mat[i]) for i in common)
        if any(self.regions[r]-n<1 for r,n in removed_regions.items()):return None
        ts=[]
        if u in self.fixed and v in self.fixed:return None
        if u in self.fixed:ts=[0.]
        elif v in self.fixed:ts=[1.]
        else:
            fixed_t=None
            constraints=list(zip(self.bounds[u],self.bounds[v]))
            if self.features is not None:
                for feature in {self.features[i] for i in self.vf[u]}:
                    lo,hi=self.feature_bounds[feature]
                    for k in range(3):
                        eps=max(1e-9,(hi[k]-lo[k])*1e-7)
                        aa=-1 if abs(self.p[u][k]-lo[k])<eps else 1 if abs(self.p[u][k]-hi[k])<eps else 0
                        bb=-1 if abs(self.p[v][k]-lo[k])<eps else 1 if abs(self.p[v][k]-hi[k])<eps else 0
                        if hi[k]-lo[k]>eps:constraints.append((aa,bb))
            for a,b in constraints:
                if a==2 or b==2:continue
                if a and b and a!=b:return None
                required=0. if a and not b else 1. if b and not a else None
                if required is not None:
                    if fixed_t is not None and fixed_t!=required:return None
                    fixed_t=required
            if fixed_t is not None:ts=[fixed_t]
            else:
                quad=self.q[u]+self.q[v];a=np.r_[self.p[u],1];d=np.r_[self.p[v]-self.p[u],0]
                den=d@quad@d
                t=max(0.,min(1.,-(d@quad@a)/den)) if den>1e-30 else .5
                ts=[t,0.,1.]
        quad=self.q[u]+self.q[v];best=None
        for t in ts:
            p=self.p[u]*(1-t)+self.p[v]*t;h=np.r_[p,1]
            cost=max(0,float(h@quad@h))
            # Prefer local short edges when surface-plane error is identical.
            cost+=np.dot(self.p[u]-self.p[v],self.p[u]-self.p[v])*1e-12
            if best is None or cost<best[0]:best=(cost,p,t)
        return best
    def push(self,u,v):
        if u>v:u,v=v,u
        value=self.candidate(u,v)
        if value is None:return
        self.serial+=1
        heapq.heappush(self.heap,(value[0],self.serial,u,v,int(self.version[u]),int(self.version[v]),value[1],value[2]))
    def valid_geometry(self,u,v,p):
        for i in self.vf[u]|self.vf[v]:
            face=self.f[i]
            if u in face and v in face:continue
            old=self.p[face];new=old.copy()
            for k,n in enumerate(face):
                if n in [u,v]:new[k]=p
            a=np.cross(old[1]-old[0],old[2]-old[0]);b=np.cross(new[1]-new[0],new[2]-new[0])
            aa=np.linalg.norm(a);bb=np.linalg.norm(b)
            if bb<max(1e-20,aa*.02) or np.dot(a,b)<aa*bb*.30:return False
        return True
    def reduce(self,budget):
        count=int(self.alive.sum());collapses=0
        while count>budget and self.heap:
            cost,serial,u,v,vu,vv,p,t=heapq.heappop(self.heap)
            if not self.va[u] or not self.va[v]:continue
            if self.version[u]!=vu or self.version[v]!=vv:
                self.push(u,v);continue
            if not self.valid_geometry(u,v,p):self.rejected['fold_or_degeneracy']+=1;continue
            common=self.vf[u]&self.vf[v]
            if not common:continue
            affected=self.vf[u]|self.vf[v];neighbors=self.neighbors(u)|self.neighbors(v)
            for i in common:
                self.alive[i]=False;count-=1
                self.comp_count[self.component[i]]-=1;self.regions[(self.component[i],self.mat[i])]-=1
                for n in self.f[i]:self.vf[n].discard(i)
            for i in list(self.vf[v]):
                self.f[i]=[u if n==v else n for n in self.f[i]];self.vf[u].add(i)
            self.vf[v].clear();self.va[v]=False;self.p[u]=p;self.q[u]+=self.q[v]
            if v in self.fixed:self.fixed.add(u)
            self.bounds[u]=tuple(a if a else b for a,b in zip(self.bounds[u],self.bounds[v]))
            for n in neighbors|{u,v}:self.version[n]+=1
            for n in neighbors:
                if self.va[n] and n!=u:self.push(u,n)
            collapses+=1
        ids=sorted({int(v) for i in np.flatnonzero(self.alive) for v in self.f[i]});mapping={v:i for i,v in enumerate(ids)}
        return dict(points=[self.p[i].tolist() for i in ids],triangles=[[mapping[int(v)] for v in self.f[i]] for i in np.flatnonzero(self.alive)],
                    materials=[self.mat[i] for i in np.flatnonzero(self.alive)],weights=[self.weights[i] for i in ids],
                    source_vertex_ids=ids,collapses=collapses,rejected=dict(self.rejected),components=len(self.comp_count))
