"""Read-only backdrop alternative from current runtime geometry, using hidden WGL.

Does not regenerate/change the model pack or invoke gameplay. Star/ring/rail shapes
follow Drawing::DrawBackdrop. Offline mesh lighting follows its fallback renderer.
Game postprocessing and MSAA are not reproduced; comparisons explicitly say so.
"""
from pathlib import Path
from PIL import Image,ImageEnhance,ImageDraw
import ctypes as c
from ctypes import wintypes as w
import hashlib,json,math,struct,subprocess

ROOT=Path(__file__).resolve().parents[1];PROJECT=ROOT.parents[1]
OUT=ROOT/'Export/Backdrop';SRC=ROOT/'Source/Backdrop';CHECK=ROOT/'Validation/20261109'
for p in [OUT,SRC,CHECK]:p.mkdir(parents=True,exist_ok=True)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(p,d):p.write_text(json.dumps(d,ensure_ascii=False,indent=2)+'\n',encoding='utf8')

def read_buildings():
    p=PROJECT/'Resource/Runtime/game-assets.bin';data=p.read_bytes();offset=0
    def u():
        nonlocal offset
        value=struct.unpack_from('<I',data,offset)[0];offset+=4;return value
    def skip(n):
        nonlocal offset
        offset+=n;assert offset<=len(data)
    def text():
        nonlocal offset
        n=u();value=data[offset:offset+n].decode();offset+=n;return value
    assert u()==0x41524d45 and u()==3
    count,banks=u(),u()
    for _ in range(banks):
        bones,clips=u(),u()
        for _ in range(bones):text();u();skip(64)
        for _ in range(u()):text();skip(8)
        skip(u()*12)
        skip(u()*16)
        for _ in range(clips):text();frames=u();skip(4+frames*bones*64+frames*12)
    needed={'building/'+n for n in ['Habitat','Terraced','Workshop','Dome']};meshes={}
    for _ in range(count):
        name,source=text(),text();vertices,bones,bank=u(),u(),u();skip(27*4)
        if name in needed:meshes[name]=dict(source=source,vertices=list(struct.iter_unpack('<21f',data[offset:offset+vertices*84])))
        skip(vertices*84)
    assert set(meshes)==needed and offset==len(data)
    return meshes,dict(file='Resource/Runtime/game-assets.bin',sha256=hashlib.sha256(data).hexdigest())

class WGL:
    def __init__(self):
        self.user=c.WinDLL('user32');self.gdi=c.WinDLL('gdi32');self.gl=c.WinDLL('opengl32')
        self.user.CreateWindowExW.argtypes=[w.DWORD,w.LPCWSTR,w.LPCWSTR,w.DWORD,c.c_int,c.c_int,c.c_int,c.c_int,w.HWND,w.HMENU,w.HINSTANCE,w.LPVOID];self.user.CreateWindowExW.restype=w.HWND
        self.user.GetDC.argtypes=[w.HWND];self.user.GetDC.restype=w.HDC
        # Built-in STATIC window stays hidden; no focus or visible app window.
        self.window=self.user.CreateWindowExW(0,'STATIC','Offline UI backdrop capture',0x80000000,0,0,2560,1600,None,None,None,None)
        assert self.window
        self.dc=self.user.GetDC(self.window)
        class PFD(c.Structure):
            _fields_=[('nSize',w.WORD),('nVersion',w.WORD),('dwFlags',w.DWORD),('iPixelType',c.c_ubyte),('cColorBits',c.c_ubyte),('cRedBits',c.c_ubyte),('cRedShift',c.c_ubyte),('cGreenBits',c.c_ubyte),('cGreenShift',c.c_ubyte),('cBlueBits',c.c_ubyte),('cBlueShift',c.c_ubyte),('cAlphaBits',c.c_ubyte),('cAlphaShift',c.c_ubyte),('cAccumBits',c.c_ubyte),('cAccumRedBits',c.c_ubyte),('cAccumGreenBits',c.c_ubyte),('cAccumBlueBits',c.c_ubyte),('cAccumAlphaBits',c.c_ubyte),('cDepthBits',c.c_ubyte),('cStencilBits',c.c_ubyte),('cAuxBuffers',c.c_ubyte),('iLayerType',c.c_ubyte),('bReserved',c.c_ubyte),('dwLayerMask',w.DWORD),('dwVisibleMask',w.DWORD),('dwDamageMask',w.DWORD)]
        p=PFD();p.nSize=c.sizeof(p);p.nVersion=1;p.dwFlags=0x24;p.cColorBits=32;p.cAlphaBits=8;p.cDepthBits=24
        self.gdi.ChoosePixelFormat.argtypes=[w.HDC,c.POINTER(PFD)];self.gdi.SetPixelFormat.argtypes=[w.HDC,c.c_int,c.POINTER(PFD)]
        fmt=self.gdi.ChoosePixelFormat(self.dc,c.byref(p));assert fmt and self.gdi.SetPixelFormat(self.dc,fmt,c.byref(p))
        self.gl.wglCreateContext.argtypes=[w.HDC];self.gl.wglCreateContext.restype=w.HANDLE
        self.gl.wglMakeCurrent.argtypes=[w.HDC,w.HANDLE];self.context=self.gl.wglCreateContext(self.dc);assert self.gl.wglMakeCurrent(self.dc,self.context)
        for name,args in dict(glClearColor=[c.c_float]*4,glColor4f=[c.c_float]*4,glVertex3f=[c.c_float]*3,glLineWidth=[c.c_float],glOrtho=[c.c_double]*6,glViewport=[c.c_int]*4,glReadPixels=[c.c_int]*4+[w.UINT,w.UINT,c.c_void_p]).items():getattr(self.gl,name).argtypes=args
        self.gl.wglGetProcAddress.argtypes=[c.c_char_p];self.gl.wglGetProcAddress.restype=c.c_void_p
        ptr=self.gl.wglGetProcAddress(b'glBlendFuncSeparate');assert ptr not in (None,1,2,3,-1)
        self.blend=c.WINFUNCTYPE(None,w.UINT,w.UINT,w.UINT,w.UINT)(ptr)
        self.gl.glEnable(0x0BE2);self.blend(0x0302,0x0303,1,0x0303)
        self.gl.glDisable(0x0BD0) # deterministic RGBA; no dither
    def begin(self,scale,depth=False):
        g=self.gl;self.size=(1280*scale,800*scale);g.glViewport(0,0,*self.size)
        g.glMatrixMode(0x1701);g.glLoadIdentity();g.glOrtho(0,1280,800,0,-65536,65536)
        g.glMatrixMode(0x1700);g.glLoadIdentity();g.glClearColor(0,0,0,0);g.glClear(0x4100)
        (g.glEnable if depth else g.glDisable)(0x0B71);g.glDepthFunc(0x0203)
    def finish(self):
        self.gl.glFinish();buf=(c.c_ubyte*(self.size[0]*self.size[1]*4))();self.gl.glReadPixels(0,0,*self.size,0x1908,0x1401,buf)
        # Separate blend gives premultiplied RGB. Export straight alpha exactly once.
        raw=bytearray(buf)
        for i in range(0,len(raw),4):
            a=raw[i+3]
            if 0<a<255:
                for j in range(3):raw[i+j]=min(255,round(raw[i+j]*255/a))
        return Image.frombytes('RGBA',self.size,bytes(raw)).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    def close(self):
        self.gl.wglMakeCurrent(self.dc,None);self.gl.wglDeleteContext.argtypes=[w.HANDLE];self.gl.wglDeleteContext(self.context)
        self.user.ReleaseDC.argtypes=[w.HWND,w.HDC];self.user.ReleaseDC(self.window,self.dc)
        self.user.DestroyWindow.argtypes=[w.HWND];self.user.DestroyWindow(self.window)

def main():
    meshes,origin=read_buildings();before=origin['sha256'];wgl=WGL();layers=[]
    def write(name,scale,image,variant=''):
        filename=f'{name}{variant}_{scale}x.png';image.save(OUT/filename)
        layers.append(dict(name=name,variant=variant or 'smooth',file=filename,width=image.width,height=image.height,sha256=sha(OUT/filename),alpha='straight'))
    def rings(g,scale):
        for j in range(8,0,-1):
            g.glColor4f(.18,.62,.7,.85 if j==1 else .025);g.glLineWidth(j*2*scale);g.glBegin(0x0002)
            for i in range(97):a=i*6.2831853/96;g.glVertex3f(972.8+math.cos(a)*115,216+math.sin(a)*145,0)
            g.glEnd()
    names=['Habitat','Terraced','Workshop','Dome']
    def skyline(g,cartoon):
        for i in range(23):
            verts=meshes['building/'+names[i%4]]['vertices'];height=40+(i*53%100);g.glBegin(0x0004)
            for k in range(0,len(verts),3):
                tri=verts[k:k+3];p=[(v[0]*.7,v[1]*.7,v[2]*height/44) for v in tri];a,b,d=p
                u=[b[q]-a[q] for q in range(3)];v=[d[q]-a[q] for q in range(3)];n=(u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]);length=math.sqrt(sum(t*t for t in n));diff=max(0,min(1,(-.35*n[0]-.45*n[1]+.82*n[2])/length)) if length>1e-8 else 1
                light=(.46 if diff<.22 else .72 if diff<.65 else 1) if cartoon else .48+.52*diff
                for src,point in zip(tri,p):
                    palette=int(src[10]);tint=(.09,.14,.19) if palette==1 else (.35,.75,.85) if palette==2 else (.25,.8,1);shade=1 if palette==3 else light
                    g.glColor4f(*(src[6+j]*(tint[j] if palette else 1)*shade for j in range(3)),src[9])
                    x,y,z=point;g.glVertex3f(i*1280/22+(x-y)*36,312+(x+y)*18-z*44,-6000+(x+y)*36+z*1296/44)
            g.glEnd()
    for scale in [1,2]:
        g=wgl.gl
        wgl.begin(scale);g.glColor4f(.4,.64,.72,.35)
        for i in range(65):
            x,y=(i*193+31)%1280,(i*71+11)%230;g.glBegin(0x0007)
            for dx,dy in [(0,0),(1,0),(1,1),(0,1)]:g.glVertex3f(x+dx,y+dy,0)
            g.glEnd()
        write('stars',scale,wgl.finish())
        wgl.begin(scale);rings(g,scale);write('gate_rings',scale,wgl.finish())
        wgl.begin(scale);g.glColor4f(.22,.42,.51,1);g.glLineWidth(3*scale);g.glBegin(1);g.glVertex3f(0,320,0);g.glVertex3f(1280,265,0);g.glEnd();write('rail',scale,wgl.finish())
        for cartoon in [False,True]:
            wgl.begin(scale,True);skyline(g,cartoon);write('distant_skyline',scale,wgl.finish(),'_cartoon' if cartoon else '')
    wgl.close();assert sha(PROJECT/origin['file'])==before
    # Editable ring/star/rail vectors retain logical coordinates and expand correctly at 2x.
    star=''.join(f'<rect x="{(i*193+31)%1280}" y="{(i*71+11)%230}" width="1" height="1" fill="#66a3b8" fill-opacity=".35"/>' for i in range(65))
    points=' '.join(f'{972.8+math.cos(i*6.2831853/96)*115:.6f},{216+math.sin(i*6.2831853/96)*145:.6f}' for i in range(97))
    ring=''.join(f'<polygon points="{points}" fill="none" stroke="#2e9eb3" stroke-width="{j*2}" stroke-opacity="{.85 if j==1 else .025}"/>' for j in range(8,0,-1))
    for name,body in [('stars',star),('gate_rings',ring),('rail','<path d="M0 320 1280 265" stroke="#386b82" stroke-width="3" fill="none"/>')]:
        (SRC/(name+'.svg')).write_text('<svg xmlns="http://www.w3.org/2000/svg" width="1280" height="800" viewBox="0 0 1280 800">'+body+'</svg>',encoding='utf8')
    # SVG strokes avoid GPU-dependent GL_LINE_WIDTH limits when exporting 2x.
    subprocess.run(['node',str(ROOT/'Tools/raster_backdrop.cjs')],check=True)
    for layer in layers:
        image=Image.open(OUT/layer['file']);assert image.mode=='RGBA' and image.getchannel('A').getbbox() and image.getchannel('A').getextrema()[0]==0
        layer['sha256']=sha(OUT/layer['file'])
    # Rectangular raster comparisons are offline alternatives, not live game captures.
    reviews=[];sheet=Image.new('RGB',(1280,3*310),'#071016');draw=ImageDraw.Draw(sheet)
    for row,cartoon in enumerate([False,True,False]):
        img=Image.new('RGBA',(1280,800),(6,11,22,255))
        for layer in ['stars_1x.png','gate_rings_1x.png','distant_skyline'+('_cartoon' if cartoon else '')+'_1x.png','rail_1x.png']:img=Image.alpha_composite(img,Image.open(OUT/layer))
        for col,factor in enumerate([1,.5,.25]):
            candidate=img.resize((round(1280*factor),round(800*factor)),Image.Resampling.BOX).resize((1280,800),Image.Resampling.BILINEAR)
            if row==2:candidate=ImageEnhance.Contrast(ImageEnhance.Color(candidate).enhance(.85)).enhance(1.05)
            fn=f'backdrop_{row}_{factor}.png';candidate.save(CHECK/fn);reviews.append(dict(file=fn,cartoon=cartoon,render_scale=factor,postprocess='illustrative desaturation .85 and contrast 1.05' if row==2 else 'none',game_capture=False))
            sheet.paste(candidate.convert('RGB').resize((426,266)),(col*426,row*310));draw.text((col*426+12,row*310+280),f'OFFLINE / {"cartoon" if cartoon else "smooth"} / {factor}x'+(' / illustrative post' if row==2 else ''),fill='white')
    sheet.save(CHECK/'backdrop_comparison_r2.png')
    save(OUT/'backdrop_manifest.json',dict(schema='erma.backdrop.layers',version=1,canvas=[1280,800],geometry_source=origin,geometry_instances=23,model_names=names,layers=layers,base_color=[.025,.045,.085,1],
        composition=['base_color','stars','gate_rings','dynamic_rotating_markers','dynamic_completion_effect','distant_skyline','dynamic_gate_label','rail','dynamic_vehicles'],
        excluded=['12 rotating gate markers','4 moving vehicles','completion effect','all text'],runtime_integration=False,
        note='Offline hidden WGL mesh capture using source triangles and Drawing.cpp projection/lighting; stars/rings/rail rasterized from editable vectors. Does not claim pixel parity with the active game backend, MSAA, postprocess, or live timings.',reviews=reviews))
    save(CHECK/'backdrop_report.json',dict(status='PASS',geometry_source_unchanged=True,layer_count=len(layers),canvas=[1280,800],scales=[1,2],dynamic_elements_baked=False,native_game_comparison=False))
    print('Built 10 separate backdrop layers; runtime pack unchanged; native integration untested.')
if __name__=='__main__':main()
