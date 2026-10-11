#pragma once
// ============================================================
// MonoApi.h — 动态解析游戏自带 Mono 运行时 (mono-2.0-bdwgc.dll) 的导出。
// 全部运行时 GetProcAddress，无链接期依赖。
// ============================================================
#include <cstdint>

typedef struct MonoDomain     MonoDomain;
typedef struct MonoImage      MonoImage;
typedef struct MonoAssembly   MonoAssembly;
typedef struct MonoClass      MonoClass;
typedef struct MonoClassField MonoClassField;
typedef struct MonoMethod     MonoMethod;
typedef struct MonoObject     MonoObject;
typedef struct MonoVTable     MonoVTable;
typedef struct MonoString     MonoString;
typedef struct MonoThread     MonoThread;
typedef struct MonoArray      MonoArray;
typedef struct MonoProperty   MonoProperty;
typedef struct MonoType       MonoType;
typedef unsigned short        gunichar2;

namespace MonoApi
{
    bool Init();   // 解析全部导出；缺关键导出返回 false
    bool Ready();

    // ---- 域 / 线程 ----
    extern MonoDomain*   (*mono_get_root_domain)();
    extern MonoDomain*   (*mono_domain_get)();
    extern MonoThread*   (*mono_thread_attach)(MonoDomain* domain);

    // ---- 程序集 / 镜像 ----
    extern MonoImage*    (*mono_image_loaded)(const char* name);
    extern MonoAssembly* (*mono_domain_assembly_open)(MonoDomain* domain, const char* name);
    extern MonoImage*    (*mono_assembly_get_image)(MonoAssembly* assembly);
    extern const char*   (*mono_image_get_name)(MonoImage* image);

    // ---- 类 ----
    extern MonoClass*    (*mono_class_from_name)(MonoImage* image, const char* name_space, const char* name);
    extern const char*   (*mono_class_get_name)(MonoClass* klass);
    extern const char*   (*mono_class_get_namespace)(MonoClass* klass);
    extern MonoClass*    (*mono_class_from_mono_type)(MonoType* type);
    extern MonoType*     (*mono_class_get_type)(MonoClass* klass);
    extern int           (*mono_class_init)(MonoClass* klass);
    extern MonoVTable*   (*mono_class_vtable)(MonoDomain* domain, MonoClass* klass);
    extern MonoClass*    (*mono_class_get_parent)(MonoClass* klass);
    extern MonoClass*    (*mono_class_get_interfaces)(MonoClass* klass, void** iter);
    extern MonoProperty* (*mono_class_get_properties)(MonoClass* klass, void** iter);

    // ---- 方法 ----
    extern MonoMethod*   (*mono_class_get_method_from_name)(MonoClass* klass, const char* name, int param_count);
    extern void*         (*mono_compile_method)(MonoMethod* method);
    extern MonoObject*   (*mono_runtime_invoke)(MonoMethod* method, void* obj, void** params, MonoObject** exc);
    extern MonoObject*   (*mono_runtime_object_init)(MonoObject* obj);
    extern MonoObject*   (*mono_method_get_object)(MonoDomain* domain, MonoMethod* method, MonoClass* klass);

    // ---- 属性 ----
    extern MonoProperty* (*mono_class_get_property_from_name)(MonoClass* klass, const char* name);
    extern MonoObject*   (*mono_property_get_value)(MonoProperty* prop, void* obj, void** params, MonoObject** exc);
    extern void          (*mono_property_set_value)(MonoProperty* prop, void* obj, void** params, MonoObject** exc);

    // ---- 字段 ----
    extern MonoClassField* (*mono_class_get_field_from_name)(MonoClass* klass, const char* name);
    extern void*           (*mono_class_get_fields)(MonoClass* klass, void** iter);
    extern const char*     (*mono_field_get_name)(MonoClassField* field);
    extern uint32_t        (*mono_field_get_offset)(MonoClassField* field);
    extern void            (*mono_field_get_value)(MonoObject* obj, MonoClassField* field, void* value);
    extern void            (*mono_field_set_value)(MonoObject* obj, MonoClassField* field, void* value);
    extern void            (*mono_field_static_get_value)(MonoVTable* vt, MonoClassField* field, void* value);
    extern void            (*mono_field_static_set_value)(MonoVTable* vt, MonoClassField* field, void* value);
    extern void*           (*mono_vtable_get_static_field_data)(MonoVTable* vtable);

    // ---- 对象 ----
    extern MonoObject*   (*mono_object_new)(MonoDomain* domain, MonoClass* klass);
    extern void*         (*mono_object_unbox)(MonoObject* obj);
    extern MonoClass*    (*mono_object_get_class)(MonoObject* obj);

    // ---- 字符串 ----
    extern MonoString*   (*mono_string_new)(MonoDomain* domain, const char* text);
    extern char*         (*mono_string_to_utf8)(MonoString* string_obj);
    extern void          (*mono_free)(void* ptr);
    extern gunichar2*    (*mono_string_chars)(MonoString* string_obj);
    extern int           (*mono_string_length)(MonoString* string_obj);

    // ---- 数组 ----
    extern MonoArray*    (*mono_array_new)(MonoDomain* domain, MonoClass* eclass, uintptr_t n);
    extern void*         (*mono_array_addr_with_size)(MonoObject* array, int size, int index);
    extern uintptr_t     (*mono_array_length)(MonoObject* array);

    // ---- 方法枚举（精确重载匹配用） ----
    extern void*         (*mono_class_get_methods)(MonoClass* klass, void** iter);
    extern const char*   (*mono_method_get_name)(MonoMethod* method);
    extern void*         (*mono_method_signature)(MonoMethod* method);   // MonoMethodSignature*
    extern unsigned int  (*mono_signature_get_param_count)(void* sig);
    extern MonoType*     (*mono_signature_get_params)(void* sig, void** iter);
    extern char*         (*mono_type_get_name)(MonoType* type);

    // ---- Type 反射 ----
    extern MonoObject*   (*mono_type_get_object)(MonoDomain* domain, MonoType* type);
    extern MonoObject*   (*mono_value_box)(MonoDomain* domain, MonoClass* klass, void* val);
}
