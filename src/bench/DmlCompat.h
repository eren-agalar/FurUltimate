#pragma once
// DirectML API (MIT-licensed header from Microsoft.AI.DirectML) targeting feature level 1.0, which is
// available in the DirectML.dll shipped with Windows 10 1903 and later. MinGW needs explicit UUID bindings.
#ifndef DML_TARGET_VERSION
#define DML_TARGET_VERSION 0x1000
#endif
// SAL annotations used by DirectML.h that MinGW does not define.
#ifndef _Maybenull_
#define _Maybenull_
#endif
#ifndef _Field_size_
#define _Field_size_(x)
#endif
#ifndef _Field_size_bytes_
#define _Field_size_bytes_(x)
#endif
#ifndef _Field_size_opt_
#define _Field_size_opt_(x)
#endif
#ifndef _Field_z_
#define _Field_z_
#endif
#ifndef _Inexpressible_
#define _Inexpressible_(x)
#endif
#ifndef _In_reads_bytes_opt_
#define _In_reads_bytes_opt_(x)
#endif
#ifndef _Out_writes_bytes_opt_
#define _Out_writes_bytes_opt_(x)
#endif
#include <d3d12.h>
#include <DirectML.h>
#ifdef __CRT_UUID_DECL
__CRT_UUID_DECL(IDMLObject, 0xc8263aac, 0x9e0c, 0x4a2d, 0x9b, 0x8e, 0x00, 0x75, 0x21, 0xa3, 0x31, 0x7c)
__CRT_UUID_DECL(IDMLDevice, 0x6dbd6437, 0x96fd, 0x423f, 0xa9, 0x8c, 0xae, 0x5e, 0x7c, 0x2a, 0x57, 0x3f)
__CRT_UUID_DECL(IDMLOperator, 0x26caae7a, 0x3081, 0x4633, 0x95, 0x81, 0x22, 0x6f, 0xbe, 0x57, 0x69, 0x5d)
__CRT_UUID_DECL(IDMLDispatchable, 0xdcb821a8, 0x1039, 0x441e, 0x9f, 0x1c, 0xb1, 0x75, 0x9c, 0x2f, 0x3c, 0xec)
__CRT_UUID_DECL(IDMLCompiledOperator, 0x6b15e56a, 0xbf5c, 0x4902, 0x92, 0xd8, 0xda, 0x3a, 0x65, 0x0a, 0xfe, 0xa4)
__CRT_UUID_DECL(IDMLOperatorInitializer, 0x427c1113, 0x435c, 0x469c, 0x86, 0x76, 0x4d, 0x5d, 0xd0, 0x72, 0xf8, 0x13)
__CRT_UUID_DECL(IDMLBindingTable, 0x29c687dc, 0xde74, 0x4e3b, 0xab, 0x00, 0x11, 0x68, 0xf2, 0xfc, 0x3c, 0xfc)
__CRT_UUID_DECL(IDMLCommandRecorder, 0xe6857a76, 0x2e3e, 0x4fdd, 0xbf, 0xf4, 0x5d, 0x2b, 0xa1, 0x0f, 0xb4, 0x53)
#endif

// ---------------------------------------------------------------------------------------------------
// ABI fix: COM methods that return a struct by value use a different calling convention in MSVC
// (hidden return pointer passed AFTER `this`) than in GCC/MinGW (hidden pointer first, or small structs
// in registers). mingw-w64 patches its own d3d12.h/d2d1.h for this (WIDL_EXPLICIT_AGGREGATE_RETURNS),
// but Microsoft's DirectML.h has no such workaround, so calling IDMLDispatchable::GetBindingProperties()
// directly from GCC-compiled code corrupts the stack and crashes. Call the vtable slot with the MSVC
// convention instead.
//
// IDMLDispatchable vtable: IUnknown (0-2), IDMLObject (3-6), IDMLDeviceChild::GetDevice (7),
// IDMLPageable (no methods), IDMLDispatchable::GetBindingProperties (8).
inline constexpr int kDmlGetBindingPropertiesSlot = 8;

inline DML_BINDING_PROPERTIES dmlBindingProperties(IDMLDispatchable* d) {
#if defined(__GNUC__) && !defined(_MSC_VER)
    using Fn = DML_BINDING_PROPERTIES*(STDMETHODCALLTYPE*)(IDMLDispatchable*, DML_BINDING_PROPERTIES*);
    Fn fn = (*reinterpret_cast<Fn* const*>(d))[kDmlGetBindingPropertiesSlot];
    DML_BINDING_PROPERTIES p{};
    fn(d, &p);
    return p;
#else
    return d->GetBindingProperties();
#endif
}
