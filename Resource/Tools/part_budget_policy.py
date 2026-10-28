"""Project-specific physical-size budget; no universal low-poly standard exists."""
import math
REVISION='silhouette_first_200_v3'
MAX_TRIANGLES=200
MIN_BUDGET=176
SIZE_SOFTENING_M=.005
REFERENCE_DIAGONAL_M=89.39270930195434
def triangle_budget(diagonal_m,reference_diagonal_m):
    fraction=math.log1p(max(0,diagonal_m)/SIZE_SOFTENING_M)/math.log1p(reference_diagonal_m/SIZE_SOFTENING_M)
    # Reserve room for closed curved shells after cosmetic details are removed.
    # A logarithmic allocation avoids reducing hand/eye geometry to a few faces
    # merely because a sparse 89 m air-guide part is the frozen largest part.
    value=MIN_BUDGET+(MAX_TRIANGLES-MIN_BUDGET)*min(1,fraction)
    return max(MIN_BUDGET,min(MAX_TRIANGLES,int(value/4)*4))
