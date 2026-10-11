#include "MonoApi.h"
#include <windows.h>

// 与 MonoApi.h 中的声明逐字对应（指针变量定义）
namespace MonoApi
{
#define P(a) a

    MonoDomain*   (*mono_get_root_domain)() = nullptr;
    MonoDomain*   (*mono_domain_get)() = nullptr;
    MonoThread*   (*mono_thread_attach)(MonoDomain* domain) = nullptr;
    MonoImage*    (*mono_image_loaded)(const char* name) = nullptr;
    MonoAssembly* (*mono_domain_assembly_open)(MonoDomain* domain, const char* name) = nullptr;
    MonoImage*    (*mono_assembly_get_image)(MonoAssembly* assembly) = nullptr;
    const char*   (*mono_image_get_name)(MonoImage* image) = nullptr;
    MonoClass*    (*mono_class_from_name)(MonoImage* image, const char* name_space, const char* name) = nullptr;
    const char*   (*mono_class_get_name)(MonoClass* klass) = nullptr;
    const char*   (*mono_class_get_namespace)(MonoClass* klass) = nullptr;
    MonoClass*    (*mono_class_from_mono_type)(MonoType* type) = nullptr;
    MonoType*     (*mono_class_get_type)(MonoClass* klass) = nullptr;
    int           (*mono_class_init)(MonoClass* klass) = nullptr;
    MonoVTable*   (*mono_class_vtable)(MonoDomain* domain, MonoClass* klass) = nullptr;
    MonoClass*    (*mono_class_get_parent)(MonoClass* klass) = nullptr;
    MonoClass*    (*mono_class_get_interfaces)(MonoClass* klass, void** iter) = nullptr;
    MonoProperty* (*mono_class_get_properties)(MonoClass* klass, void** iter) = nullptr;
    MonoMethod*   (*mono_class_get_method_from_name)(MonoClass* klass, const char* name, int param_count) = nullptr;
    void*         (*mono_compile_method)(MonoMethod* method) = nullptr;
    MonoObject*   (*mono_runtime_invoke)(MonoMethod* method, void* obj, void** params, MonoObject** exc) = nullptr;
    MonoObject*   (*mono_runtime_object_init)(MonoObject* obj) = nullptr;
    MonoObject*   (*mono_method_get_object)(MonoDomain* domain, MonoMethod* method, MonoClass* klass) = nullptr;
    MonoProperty* (*mono_class_get_property_from_name)(MonoClass* klass, const char* name) = nullptr;
    MonoObject*   (*mono_property_get_value)(MonoProperty* prop, void* obj, void** params, MonoObject** exc) = nullptr;
    void          (*mono_property_set_value)(MonoProperty* prop, void* obj, void** params, MonoObject** exc) = nullptr;
    MonoClassField* (*mono_class_get_field_from_name)(MonoClass* klass, const char* name) = nullptr;
    void*         (*mono_class_get_fields)(MonoClass* klass, void** iter) = nullptr;
    const char*   (*mono_field_get_name)(MonoClassField* field) = nullptr;
    uint32_t      (*mono_field_get_offset)(MonoClassField* field) = nullptr;
    void          (*mono_field_get_value)(MonoObject* obj, MonoClassField* field, void* value) = nullptr;
    void          (*mono_field_set_value)(MonoObject* obj, MonoClassField* field, void* value) = nullptr;
    void          (*mono_field_static_get_value)(MonoVTable* vt, MonoClassField* field, void* value) = nullptr;
    void          (*mono_field_static_set_value)(MonoVTable* vt, MonoClassField* field, void* value) = nullptr;
    void*         (*mono_vtable_get_static_field_data)(MonoVTable* vtable) = nullptr;
    MonoObject*   (*mono_object_new)(MonoDomain* domain, MonoClass* klass) = nullptr;
    void*         (*mono_object_unbox)(MonoObject* obj) = nullptr;
    MonoClass*    (*mono_object_get_class)(MonoObject* obj) = nullptr;
    MonoString*   (*mono_string_new)(MonoDomain* domain, const char* text) = nullptr;
    char*         (*mono_string_to_utf8)(MonoString* string_obj) = nullptr;
    void          (*mono_free)(void* ptr) = nullptr;
    gunichar2*    (*mono_string_chars)(MonoString* string_obj) = nullptr;
    int           (*mono_string_length)(MonoString* string_obj) = nullptr;
    MonoArray*    (*mono_array_new)(MonoDomain* domain, MonoClass* eclass, uintptr_t n) = nullptr;
    void*         (*mono_array_addr_with_size)(MonoObject* array, int size, int index) = nullptr;
    uintptr_t     (*mono_array_length)(MonoObject* array) = nullptr;
    void*         (*mono_class_get_methods)(MonoClass* klass, void** iter) = nullptr;
    const char*   (*mono_method_get_name)(MonoMethod* method) = nullptr;
    void*         (*mono_method_signature)(MonoMethod* method) = nullptr;
    unsigned int  (*mono_signature_get_param_count)(void* sig) = nullptr;
    MonoType*     (*mono_signature_get_params)(void* sig, void** iter) = nullptr;
    char*         (*mono_type_get_name)(MonoType* type) = nullptr;
    MonoObject*   (*mono_type_get_object)(MonoDomain* domain, MonoType* type) = nullptr;
    MonoObject*   (*mono_value_box)(MonoDomain* domain, MonoClass* klass, void* val) = nullptr;

#undef P

    static bool s_ready = false;

    bool Ready() { return s_ready; }

    bool Init()
    {
        HMODULE h = GetModuleHandleW(L"mono-2.0-bdwgc.dll");
        if (!h)
            return false;

#define YY_RESOLVE(name) \
    do { \
        name = reinterpret_cast<decltype(name)>(GetProcAddress(h, #name)); \
        if (!name) { return false; } \
    } while (0)

        // 关键导出（缺一不可）
        YY_RESOLVE(mono_get_root_domain);
        YY_RESOLVE(mono_domain_get);
        YY_RESOLVE(mono_thread_attach);
        YY_RESOLVE(mono_image_loaded);
        YY_RESOLVE(mono_class_from_name);
        YY_RESOLVE(mono_class_get_name);
        YY_RESOLVE(mono_class_init);
        YY_RESOLVE(mono_class_vtable);
        YY_RESOLVE(mono_class_get_method_from_name);
        YY_RESOLVE(mono_runtime_invoke);
        YY_RESOLVE(mono_object_new);
        YY_RESOLVE(mono_object_unbox);
        YY_RESOLVE(mono_string_new);
        YY_RESOLVE(mono_string_to_utf8);
        YY_RESOLVE(mono_free);
        YY_RESOLVE(mono_array_new);
        YY_RESOLVE(mono_array_addr_with_size);
        YY_RESOLVE(mono_array_length);
        YY_RESOLVE(mono_field_get_value);
        YY_RESOLVE(mono_field_set_value);
        YY_RESOLVE(mono_class_get_field_from_name);
        YY_RESOLVE(mono_field_get_offset);
        YY_RESOLVE(mono_field_get_name);
        YY_RESOLVE(mono_object_get_class);
        YY_RESOLVE(mono_value_box);
        YY_RESOLVE(mono_type_get_object);
        YY_RESOLVE(mono_class_get_type);
        YY_RESOLVE(mono_class_get_methods);
        YY_RESOLVE(mono_method_get_name);
        YY_RESOLVE(mono_method_signature);
        YY_RESOLVE(mono_signature_get_param_count);
        YY_RESOLVE(mono_signature_get_params);
        YY_RESOLVE(mono_type_get_name);

        // 可选导出（缺了降级处理）
        mono_assembly_get_image        = reinterpret_cast<decltype(mono_assembly_get_image)>(GetProcAddress(h, "mono_assembly_get_image"));
        mono_domain_assembly_open      = reinterpret_cast<decltype(mono_domain_assembly_open)>(GetProcAddress(h, "mono_domain_assembly_open"));
        mono_image_get_name            = reinterpret_cast<decltype(mono_image_get_name)>(GetProcAddress(h, "mono_image_get_name"));
        mono_class_get_namespace       = reinterpret_cast<decltype(mono_class_get_namespace)>(GetProcAddress(h, "mono_class_get_namespace"));
        mono_class_from_mono_type      = reinterpret_cast<decltype(mono_class_from_mono_type)>(GetProcAddress(h, "mono_class_from_mono_type"));
        mono_class_get_parent          = reinterpret_cast<decltype(mono_class_get_parent)>(GetProcAddress(h, "mono_class_get_parent"));
        mono_class_get_interfaces      = reinterpret_cast<decltype(mono_class_get_interfaces)>(GetProcAddress(h, "mono_class_get_interfaces"));
        mono_class_get_properties      = reinterpret_cast<decltype(mono_class_get_properties)>(GetProcAddress(h, "mono_class_get_properties"));
        mono_compile_method            = reinterpret_cast<decltype(mono_compile_method)>(GetProcAddress(h, "mono_compile_method"));
        mono_runtime_object_init       = reinterpret_cast<decltype(mono_runtime_object_init)>(GetProcAddress(h, "mono_runtime_object_init"));
        mono_method_get_object         = reinterpret_cast<decltype(mono_method_get_object)>(GetProcAddress(h, "mono_method_get_object"));
        mono_class_get_property_from_name = reinterpret_cast<decltype(mono_class_get_property_from_name)>(GetProcAddress(h, "mono_class_get_property_from_name"));
        mono_property_get_value        = reinterpret_cast<decltype(mono_property_get_value)>(GetProcAddress(h, "mono_property_get_value"));
        mono_property_set_value        = reinterpret_cast<decltype(mono_property_set_value)>(GetProcAddress(h, "mono_property_set_value"));
        mono_class_get_fields          = reinterpret_cast<decltype(mono_class_get_fields)>(GetProcAddress(h, "mono_class_get_fields"));
        mono_field_static_get_value    = reinterpret_cast<decltype(mono_field_static_get_value)>(GetProcAddress(h, "mono_field_static_get_value"));
        mono_field_static_set_value    = reinterpret_cast<decltype(mono_field_static_set_value)>(GetProcAddress(h, "mono_field_static_set_value"));
        mono_vtable_get_static_field_data = reinterpret_cast<decltype(mono_vtable_get_static_field_data)>(GetProcAddress(h, "mono_vtable_get_static_field_data"));
        mono_string_chars              = reinterpret_cast<decltype(mono_string_chars)>(GetProcAddress(h, "mono_string_chars"));
        mono_string_length             = reinterpret_cast<decltype(mono_string_length)>(GetProcAddress(h, "mono_string_length"));

#undef YY_RESOLVE

        s_ready = true;
        return true;
    }
}
