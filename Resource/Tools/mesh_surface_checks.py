"""Symmetric sampled surface distance for Blender mesh review (not Hausdorff proof)."""
from mathutils import Vector
from mathutils.bvhtree import BVHTree

def surface(points,triangles):
    return BVHTree.FromPolygons([Vector(p) for p in points],triangles,all_triangles=True)

def errors(a,af,b,bf):
    ta,tb=surface(a,af),surface(b,bf);maximum=0
    for points,faces,target in [(a,af,tb),(b,bf,ta)]:
        samples=[Vector(p) for p in points]
        samples.extend(sum((Vector(points[i]) for i in f),Vector())/3 for f in faces)
        for p in samples:
            hit=target.find_nearest(p)
            if hit:maximum=max(maximum,hit[3])
    return maximum
