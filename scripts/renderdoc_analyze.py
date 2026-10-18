"""Run inside qrenderdoc --python; inspect one real-application capture."""
import hashlib
import json
import os
import re
import time
import traceback
from pathlib import Path
import renderdoc as rd

out=Path(os.environ['NANOVA_RENDERDOC_OUTPUT'])
report={'capture':os.environ['NANOVA_RENDERDOC_CAPTURE_FILE']}
controller=None
capture=None
try:
    capture=rd.OpenCaptureFile()
    status=capture.OpenFile(report['capture'],'',None)
    report['open_file']=str(status)
    if status!=rd.ResultCode.Succeeded:raise RuntimeError(str(status))
    started=time.monotonic()
    options=rd.ReplayOptions();options.apiValidation=True
    status,controller=capture.OpenCapture(options,None)
    report['open_replay']=str(status)
    if status!=rd.ResultCode.Succeeded:raise RuntimeError(str(status))
    structured=controller.GetStructuredFile()
    actions=[]
    def visit(items):
        for action in items:
            actions.append(action)
            visit(action.children)
    visit(controller.GetRootActions())
    counters=controller.EnumerateCounters()
    report['available_counters']=[]
    for counter in counters:
        description=controller.DescribeCounter(counter)
        report['available_counters'].append(dict(id=int(counter),name=description.name,
            description=description.description,type=str(description.resultType),unit=str(description.unit)))
    if rd.GPUCounter.EventGPUDuration not in counters:
        raise RuntimeError('Replay exposes no GPU duration counter')
    chosen=[c for c in (rd.GPUCounter.EventGPUDuration,rd.GPUCounter.CSInvocations) if c in counters]
    values={}
    for result in controller.FetchCounters(chosen):
        value=result.value.d if result.counter==rd.GPUCounter.EventGPUDuration else result.value.u64
        values[(result.eventId,int(result.counter))]=value
    report['events']=[]
    for action in actions:
        duration=values.get((action.eventId,int(rd.GPUCounter.EventGPUDuration)))
        report['events'].append(dict(event=action.eventId,name=action.GetName(structured),
            dispatch=list(action.dispatchDimension),gpu_ms=duration*1000 if duration is not None else None,
            compute_invocations=values.get((action.eventId,int(rd.GPUCounter.CSInvocations)))))
    report['event_duration_sum_ms']=sum(item['gpu_ms'] for item in report['events'] if item['gpu_ms'] is not None)
    report['duration_event_count']=sum(item['gpu_ms'] is not None for item in report['events'])
    report['disassembly_targets']=list(controller.GetDisassemblyTargets(True))
    report['shaders']=[]
    shader_folder=Path(os.environ['NANOVA_RENDERDOC_SHADER_FOLDER'])
    known={hashlib.sha256(p.read_bytes()).hexdigest():p.name for p in shader_folder.glob('*.spv')}
    # Identify the surface and volume programs by captured SPIR-V hashes.
    # Spatial-volume programs have different dispatch dimensions.
    for action in actions:
        if not action.flags & rd.ActionFlags.Dispatch:continue
        controller.SetFrameEvent(action.eventId,False)
        pipeline=controller.GetPipelineState()
        reflection=pipeline.GetShaderReflection(rd.ShaderStage.Compute)
        if reflection is None:continue
        digest=hashlib.sha256(bytes(reflection.rawBytes)).hexdigest()
        name=known.get(digest,'unknown')
        if name not in ('debug_guide_surface.comp.spv','debug_surface.comp.spv','ray_trace.comp.spv','volume.comp.spv','debug_volume.comp.spv','volume_coarse.comp.spv','volume_filter.comp.spv'):continue
        record=dict(event=action.eventId,shader=name,sha256=digest,threads=list(reflection.dispatchThreadsDimension),dispatch=list(action.dispatchDimension))
        target='KHR_pipeline_executable_properties'
        if target in report['disassembly_targets']:
            text=controller.DisassembleShader(pipeline.GetComputePipelineObject(),reflection,target)
            filename='shader-'+str(action.eventId)+'-driver.txt'
            (out/filename).write_text(text)
            record['driver_report']=filename
            record['compiler_statistics']={}
            for key in ('Subgroup Size','SGPRs','VGPRs','Spilled SGPRs','Spilled VGPRs','Code size','LDS size','Scratch size','Subgroups per SIMD','Instructions'):
                match=re.search(r'^'+re.escape(key)+r': (\d+)',text,re.M)
                if match:record['compiler_statistics'][key]=int(match.group(1))
        report['shaders'].append(record)
    names={s['shader'] for s in report['shaders']}
    surface=bool(names&{'debug_guide_surface.comp.spv','debug_surface.comp.spv','ray_trace.comp.spv'})
    volume=bool(names&{'volume.comp.spv','debug_volume.comp.spv'}) or {'volume_coarse.comp.spv','volume_filter.comp.spv'}<=names
    if not surface or not volume:raise RuntimeError('Could not identify the surface and volume programs')
    report['replay_analysis_seconds']=time.monotonic()-started
except BaseException:
    report['error']=traceback.format_exc()
finally:
    if controller:controller.Shutdown()
    if capture:capture.Shutdown()
    (out/'analysis.json').write_text(json.dumps(report,indent=2)+'\n')
    # --python runs before the main UI opens. Leave no background GUI process.
    os._exit(1 if 'error' in report else 0)
