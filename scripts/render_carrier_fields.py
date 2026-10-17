import json
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.tri import Triangulation
from pathlib import Path
import argparse
parser=argparse.ArgumentParser(description="Plot actual saved MFEM carrier fields; optional matplotlib/numpy visualization")
parser.add_argument("project",type=Path)
parser.add_argument("output",type=Path)
parser.add_argument("--caption",default="Static reference study; convergence and acceptance require the analysis report")
args=parser.parse_args()
data=json.load(open(args.project))
run=data['engineeringModel']['structuralStudies'][0]['runs'][0]
mesh=run['meshSnapshot'];points=np.array(mesh['nodePositionsM']);tet=np.array(mesh['tetrahedra'])
faces=np.concatenate([tet[:,f] for f in [[0,1,2],[0,1,3],[0,2,3],[1,2,3]]])
owners=np.tile(np.arange(len(tet)),4)
keys=np.sort(faces,axis=1);_,indices,counts=np.unique(keys,axis=0,return_index=True,return_counts=True)
indices=indices[counts==1];top=indices[np.all(np.isclose(points[faces[indices],2],points[:,2].max(),rtol=0,atol=1e-12),axis=1)]
tri=Triangulation(points[:,0]*1000,points[:,1]*1000,faces[top]);fields={f['fieldType']:np.array(f['values']) for f in run['fields']}
u=fields['displacement'].reshape(-1,3);vm=fields['von-mises-stress'] if 'von-mises-stress' in fields else fields['von_mises_stress']
fig,axes=plt.subplots(1,2,figsize=(10,6),layout='constrained')
left=axes[0].tripcolor(tri,np.linalg.norm(u,axis=1)*1e6,shading='gouraud',cmap='viridis',rasterized=True)
right=axes[1].tripcolor(tri,facecolors=vm[owners[top]]/1e6,shading='flat',cmap='magma',rasterized=True)
for ax,title in zip(axes,['Displacement magnitude','Element von Mises stress']):
 ax.set(aspect='equal',xlabel='x [mm]',ylabel='y [mm]',title=title,xlim=(-18,18),ylim=(-23,23));ax.set_facecolor('#eeeeee')
 ax.plot([-11,11],[-15,-15],'ws',ms=4,mec='black',label='Ideal M4 restraints')
 ax.legend(loc='lower left',fontsize=7)
fig.colorbar(left,ax=axes[0],label='µm',shrink=.8);fig.colorbar(right,ax=axes[1],label='MPa',shrink=.8)
if not run['provenance']['solverId'].startswith('mfem.serial-'):
 raise ValueError('Expected actual serial MFEM result provenance')
fig.suptitle(f"Actual MFEM CPU fields • {len(tet):,} linear tetrahedra\n{args.caption}",fontsize=11)
fig.savefig(args.output,dpi=170)
