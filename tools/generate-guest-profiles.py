"""Extract version-specific hook addresses and prologue guards from local images."""
import importlib.util
from pathlib import Path
import struct

root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('inspect_guest',root/'tools/inspect-android-vr.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
names={
 'render':'_Z13R_RenderSceneP8refdef_tP12GfxViewParms',
 'setup':'_Z27R_SetupRenderSceneViewParmsP8refdef_t',
 'view':'_Z22R_SetViewParmsForSceneP8refdef_tP12GfxViewParms',
 'cell':'_Z14R_CellForPointP6vec3_t',
 'createCmd':'_Z12CL_CreateCmdjP9usercmd_s',
 'finishMove':'_Z13CL_FinishMovejP9usercmd_s',
 'setAngles':'_Z16CL_SetViewAnglesjP6vec3_t',
 'weapon':'_Z41CG_CalculateWeaponMovement_ClientSpecificP4cg_tP18GfxScaledPlacementP6vec3_t',
 'crosshair':'_Z16CG_DrawCrosshairj',
 'backend':'_Z23RB_RenderThreadFrameArmP14GfxBackEndData',
 'draw3d':'_Z9RB_Draw3Dv',
 'setViewport':'_Z13R_SetViewportP14GfxCmdBufStateP11GfxViewport',
 'setScissor':'_Z12R_SetScissorP11GfxViewport',
 'flame':'_Z10RB_FlameFXP11GfxViewInfo',
 'shake':'_Z14CG_ShakeCameraj',
 'perturb':'_Z16CG_PerturbCameraP4cg_t',
 'damageKick':'_Z35BG_CalculateViewMovement_DamageKickP11viewState_tP6vec3_t',
 'shellshockBlend':'_Z19DrawShellshockBlendj',
}
module.TARGETS={v:k for k,v in names.items()}|{'frontEndDataOut':'frontend buffer','backEndData':'backend buffer','gfxCmdBufState':'command state','cgArray':'client game state'}
rows=[]
for mode in ['zm','mp']:
    path=root/f'research/t6requiem-apk/assets/guest/t6{mode}_guest.elf'
    info=module.inspect(path);assert not info['missing']
    data=path.read_bytes();syms=info['symbols']
    # CL_SetViewAngles loads clientActive through an ADRP + LDR GOT slot.
    set_symbol=syms[names['setAngles']]
    pc=int(set_symbol['guest_virtual_address'],16)+0x30
    off=int(set_symbol['file_offset'],16)+0x30
    adrp,ldr=struct.unpack_from('<II',data,off)
    assert adrp&0x9f00001f==0x90000001 and ldr&0xffc003ff==0xb9400021
    imm=((adrp>>5)&0x7ffff)<<2 | ((adrp>>29)&3)
    if imm&(1<<20):imm-=1<<21
    got=(pc&~0xfff)+(imm<<12)+((ldr>>10)&0xfff)*4
    fields=[f'"{mode}"',f'"{info["sha256"]}"',hex(got),syms['frontEndDataOut']['guest_virtual_address'],syms['backEndData']['guest_virtual_address'],syms['gfxCmdBufState']['guest_virtual_address']]
    fields.append(syms['cgArray']['guest_virtual_address'])
    for key,name in names.items():
        sym=syms[name]
        words=struct.unpack('<4I',bytes.fromhex(sym['first_16_bytes']))
        fields.append('{'+sym['guest_virtual_address']+', {'+','.join(hex(w) for w in words)+'}}')
    rows.append('    {'+',\n     '.join(fields)+'}')
header='''// Generated from the locally verified Beta 2.3 images. Do not hand-edit.
#pragma once
#include <cstdint>
struct GuestFunction {uintptr_t offset;uint32_t prologue[4];};
struct GuestProfile {const char* mode;const char* sha256;uintptr_t clientActiveGot,frontendData,backendData,cmdState,cgArray;
    GuestFunction render,setup,view,cell,createCmd,finishMove,setAngles,weapon,crosshair,backend,draw3d,setViewport,setScissor,flame,shake,perturb,damageKick,shellshockBlend;};
inline constexpr GuestProfile guestProfiles[]={
'''+',\n'.join(rows)+'\n};\n'
(root/'src/quest/guest_profiles.h').write_text(header)
print('Generated guarded Zombies and Multiplayer profiles.')
