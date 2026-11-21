.text
.align 2
.global quest_guest_render_entry
.hidden quest_guest_render_entry
.type quest_guest_render_entry,%function
quest_guest_render_entry:
    stp x29,x30,[sp,#-32]!
    mov x29,sp
    mrs x2,tpidr_el0
    str x2,[sp,#16]
    ldr x3,[x2,#16]
    msr tpidr_el0,x3
    bl quest_guest_render_callback
    ldr x2,[sp,#16]
    msr tpidr_el0,x2
    ldp x29,x30,[sp],#32
    ret
.size quest_guest_render_entry,.-quest_guest_render_entry

.global quest_guest_setup_entry
.hidden quest_guest_setup_entry
.type quest_guest_setup_entry,%function
quest_guest_setup_entry:
    stp x29,x30,[sp,#-32]!
    mov x29,sp
    mrs x2,tpidr_el0
    str x2,[sp,#16]
    ldr x3,[x2,#16]
    msr tpidr_el0,x3
    bl quest_guest_setup_callback
    ldr x2,[sp,#16]
    msr tpidr_el0,x2
    ldp x29,x30,[sp],#32
    ret
.size quest_guest_setup_entry,.-quest_guest_setup_entry

.macro guest_entry name,callback,tlsarg
.global \name
.hidden \name
.type \name,%function
\name:
    stp x29,x30,[sp,#-32]!
    mov x29,sp
    mrs \tlsarg,tpidr_el0
    str \tlsarg,[sp,#16]
    ldr x9,[\tlsarg,#16]
    msr tpidr_el0,x9
    bl \callback
    ldr x9,[sp,#16]
    msr tpidr_el0,x9
    ldp x29,x30,[sp],#32
    ret
.size \name,.-\name
.endm
guest_entry quest_guest_cmd_entry,quest_guest_cmd_callback,x2
guest_entry quest_guest_finish_entry,quest_guest_finish_callback,x2
guest_entry quest_guest_weapon_entry,quest_guest_weapon_callback,x3
guest_entry quest_guest_crosshair_entry,quest_guest_crosshair_callback,x1
guest_entry quest_guest_backend_entry,quest_guest_backend_callback,x1
guest_entry quest_guest_draw3d_entry,quest_guest_draw3d_callback,x0
guest_entry quest_guest_viewport_entry,quest_guest_viewport_callback,x2
guest_entry quest_guest_scissor_entry,quest_guest_scissor_callback,x1
guest_entry quest_guest_flame_entry,quest_guest_flame_callback,x1
guest_entry quest_guest_shake_entry,quest_guest_shake_callback,x1
guest_entry quest_guest_perturb_entry,quest_guest_perturb_callback,x1
guest_entry quest_guest_damagekick_entry,quest_guest_damagekick_callback,x2
guest_entry quest_guest_shellshock_entry,quest_guest_shellshock_callback,x1

// Called on the guest's low-address stack with host TLS. Switch only TLS
// while invoking an ILP32 guest function; pointer arguments are below 4 GiB.
.global quest_call_guest
.hidden quest_call_guest
.type quest_call_guest,%function
quest_call_guest:
    stp x29,x30,[sp,#-32]!
    mov x29,sp
    mrs x9,tpidr_el0
    str x9,[sp,#16]
    mov x8,x0
    msr tpidr_el0,x1
    mov w0,w2
    mov w1,w3
    mov w2,w4
    blr x8
    ldr x9,[sp,#16]
    msr tpidr_el0,x9
    ldp x29,x30,[sp],#32
    ret
.size quest_call_guest,.-quest_call_guest
.section .note.GNU-stack,"",%progbits
