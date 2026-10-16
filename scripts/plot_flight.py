#!/usr/bin/env python3
"""Plot sampled real-application flight paths from the navigation E2E export."""
import argparse
import json
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Circle

FIGURE_WIDTH_INCHES = 16
FIGURE_HEIGHT_INCHES = 9
FIGURE_DPI = 120
CELL_ALPHA = .14
FACTORY_HEAD_ALPHA = .4
CRAFT_PALETTE_SIZE = 10
PATH_LINE_WIDTH = 1.7
PATH_ALPHA = .9
FINAL_POINT_SIZE = 45
GRID_ALPHA = .3
TITLE_FONT_SIZE = 22
SIMULATION_TICKS_PER_SECOND = 60
FOOTNOTE_FONT_SIZE = 11

PATH_POINT_SIZE=9
LEGEND_FONT_SIZE=9
PLOT_HALF_EXTENT=10

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('samples',type=Path)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args();data=json.loads(args.samples.read_text())
center=data['bodies'][0]['position'];radius=data['bodies'][0]['radius']
fig,axes=plt.subplots(1,2,figsize=(FIGURE_WIDTH_INCHES,FIGURE_HEIGHT_INCHES),dpi=FIGURE_DPI,layout='constrained')
fig.patch.set_facecolor('#111a24')
colors=plt.get_cmap('tab10')
for axis,dims,title in zip(axes,[(0,1),(0,2)],['Front view: X / Y','Top view: X / Z']):
    axis.set_facecolor('#172433')
    axis.add_patch(Circle((0,0),radius,color='#78a5ac',alpha=CELL_ALPHA,label='Cell'))
    for o in data['obstacles']:
        if o['cell']!=0:continue
        a=[o['start'][i]-center[i] for i in dims];b=[o['end'][i]-center[i] for i in dims]
        if o['start']==o['end']:axis.add_patch(Circle(a,o['radius'],color='#bccad2',alpha=FACTORY_HEAD_ALPHA))
        else:axis.plot([a[0],b[0]],[a[1],b[1]],color='#a7bbc4',linewidth=2,alpha=.5)
    generations=sorted({u['generation'] for u in data['samples'][0]['units'] if u['faction']==1})
    for index,generation in enumerate(generations):
        points=[u['position'] for sample in data['samples'] for u in sample['units'] if u['generation']==generation]
        x=[p[dims[0]]-center[dims[0]] for p in points];y=[p[dims[1]]-center[dims[1]] for p in points]
        axis.plot(x,y,color=colors(index%CRAFT_PALETTE_SIZE),linewidth=PATH_LINE_WIDTH,alpha=PATH_ALPHA,label='Craft '+str(generation))
        axis.scatter(x,y,color=[colors(index%CRAFT_PALETTE_SIZE)],s=PATH_POINT_SIZE)
        axis.scatter(x[-1],y[-1],color=[colors(index%CRAFT_PALETTE_SIZE)],s=FINAL_POINT_SIZE,marker='s')
    axis.set(xlim=(-PLOT_HALF_EXTENT,PLOT_HALF_EXTENT),ylim=(-PLOT_HALF_EXTENT,PLOT_HALF_EXTENT),aspect='equal',title=title,xlabel='World units from cell center',ylabel='World units from cell center')
    axis.tick_params(colors='#c9d5df');axis.xaxis.label.set_color('#c9d5df');axis.yaxis.label.set_color('#c9d5df');axis.title.set_color('#eff5fa')
    for spine in axis.spines.values():spine.set_color('#4d667a')
    axis.grid(color='#385065',alpha=GRID_ALPHA)
axes[1].legend(loc='upper left',fontsize=LEGEND_FONT_SIZE,facecolor='#172433',labelcolor='#d8e4ee',edgecolor='#4d667a')
fig.suptitle('Measured patrol paths around live factories',color='#eff5fa',fontsize=TITLE_FONT_SIZE)
interval=(data['samples'][1]['tick']-data['samples'][0]['tick'])/SIMULATION_TICKS_PER_SECOND
fig.supxlabel(f'Real Windows application through Proton. Points are {interval:g} seconds apart. Lines join recorded positions. Squares mark final positions.\nFactory geometry is shown at the end of the 30-second run. No factory collision corrections were recorded.',color='#c9d5df',fontsize=FOOTNOTE_FONT_SIZE)
args.output.parent.mkdir(parents=True,exist_ok=True)
fig.savefig(args.output)
fig.savefig(args.output.with_suffix('.png'))
print('Saved flight path plot:',args.output)
