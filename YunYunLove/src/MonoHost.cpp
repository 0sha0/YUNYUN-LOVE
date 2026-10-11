#include "MonoHost.h"
#include "MainThread.h"
#include "Log.h"
#include <windows.h>
#include <cstring>

namespace MonoHost
{
    using namespace MonoApi;

    static MonoClass* s_string = nullptr;
    static MonoClass* s_type = nullptr;
    static MonoClass* s_object = nullptr;
    static MonoClass* s_single = nullptr;
    static MonoClass* s_int32 = nullptr;
    static MonoClass* s_int64 = nullptr;
    static MonoClass* s_boolean = nullptr;

    static MonoDomain* Dom()
    {
        MonoDomain* dom = mono_domain_get ? mono_domain_get() : nullptr;
        if (!dom)
            dom = mono_get_root_domain();
        return dom;
    }

    bool Init()
    {
        if (!MonoApi::Ready())
            return false;
        MonoDomain* dom = Dom();
        if (!dom)
            return false;

        if (!s_string)
        {
            s_string  = mono_class_from_name(mono_image_loaded("mscorlib"), "System", "String");
            s_object  = mono_class_from_name(mono_image_loaded("mscorlib"), "System", "Object");
            s_type    = mono_class_from_name(mono_image_loaded("mscorlib"), "System", "Type");
            s_single  = mono_class_from_name(mono_image_loaded("mscorlib"), "System", "Single");
            s_int32   = mono_class_from_name(mono_image_loaded("mscorlib"), "System", "Int32");
            s_int64   = mono_class_from_name(mono_image_loaded("mscorlib"), "System", "Int64");
            s_boolean = mono_class_from_name(mono_image_loaded("mscorlib"), "System", "Boolean");
            if (!s_string || !s_type || !s_int32 || !s_boolean)
            {
                Log::Printf("[MonoHost] core class resolve failed (string=%p type=%p int=%p bool=%p)",
                            (void*)s_string, (void*)s_type, (void*)s_int32, (void*)s_boolean);
                return false;
            }
            mono_class_init(s_type);
        }
        return true;
    }

    // ---------- 镜像 / 类 ----------
    MonoImage* Image(const char* name)
    {
        MonoImage* img = mono_image_loaded(name);
        if (!img && mono_domain_assembly_open)
        {
            MonoDomain* dom = Dom();
            std::string path = std::string("Managed/") + name + ".dll";
            MonoAssembly* asm_ = mono_domain_assembly_open(dom, path.c_str());
            if (asm_ && mono_assembly_get_image)
                img = mono_assembly_get_image(asm_);
        }
        return img;
    }

    MonoClass* Class(MonoImage* img, const char* ns, const char* name)
    {
        if (!img)
            return nullptr;
        MonoClass* c = mono_class_from_name(img, ns, name);
        return c;
    }

    std::string ClassName(MonoClass* cls)
    {
        if (!cls || !mono_class_get_name)
            return {};
        const char* ns = mono_class_get_namespace ? mono_class_get_namespace(cls) : "";
        std::string s;
        if (ns && ns[0])
        {
            s = ns;
            s += ".";
        }
        s += mono_class_get_name(cls);
        return s;
    }

    // ---------- 字符串 ----------
    MonoObject* Str(const char* utf8)
    {
        MonoDomain* dom = Dom();
        if (!dom)
            return nullptr;
        return (MonoObject*)mono_string_new(dom, utf8 ? utf8 : "");
    }

    MonoObject* StrW(const std::wstring& w)
    {
        // 宽字符 → utf8 → mono 字符串（中文歌名安全通道）
        std::string u8;
        int need = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        if (need > 0)
        {
            u8.resize(need);
            WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), u8.data(), need, nullptr, nullptr);
        }
        return Str(u8.c_str());
    }

    std::string StrUtf8(MonoObject* str)
    {
        if (!str)
            return {};
        char* p = mono_string_to_utf8((MonoString*)str);
        if (!p)
            return {};
        std::string s = p;
        mono_free(p);
        return s;
    }

    std::wstring StrWide(MonoObject* str)
    {
        if (!str || !mono_string_chars || !mono_string_length)
            return {};
        MonoString* s = (MonoString*)str;
        int len = mono_string_length(s);
        if (len <= 0)
            return {};
        gunichar2* chars = mono_string_chars(s);
        if (!chars)
            return {};
        return std::wstring(chars, chars + len);
    }

    // ---------- 异常文本 ----------
    static std::string ExcText(MonoObject* exc)
    {
        if (!exc)
            return "unknown";
        MonoClass* c = mono_object_get_class(exc);
        MonoMethod* ts = c ? mono_class_get_method_from_name(c, "ToString", 0) : nullptr;
        if (ts)
        {
            Inv r = Invoke(ts, exc, nullptr);
            if (r.ok && r.ret)
                return StrUtf8(r.ret);
        }
        return ClassName(c);
    }

    // ---------- 调用 ----------
    MonoMethod* FindMethodExact(MonoClass* cls, const char* name, int argc, const char* firstParamType)
    {
        if (!cls || !mono_class_get_methods || !mono_method_get_name ||
            !mono_method_signature || !mono_signature_get_param_count)
            return nullptr;
        void* iter = nullptr;
        MonoMethod* m = nullptr;
        while ((m = (MonoMethod*)mono_class_get_methods(cls, &iter)) != nullptr)
        {
            const char* mn = mono_method_get_name(m);
            if (!mn || strcmp(mn, name) != 0)
                continue;
            void* sig = mono_method_signature(m);
            if (!sig || (int)mono_signature_get_param_count(sig) != argc)
                continue;
            if (firstParamType)
            {
                // 取首参类型全名比对（System.Type / System.Boolean / ...）
                // 用导出迭代器读参数（不依赖 MonoMethodSignature 结构布局）
                if (!mono_signature_get_params)
                    return m;   // 缺导出则退化为参数个数匹配
                void* pit = nullptr;
                MonoType* pt = mono_signature_get_params(sig, &pit);
                if (!pt)
                    continue;
                char* tn = mono_type_get_name(pt);
                if (!tn)
                    continue;
                bool ok = strcmp(tn, firstParamType) == 0;
                mono_free(tn);
                if (!ok)
                    continue;
            }
            return m;
        }
        return nullptr;
    }

    Inv Invoke(MonoMethod* m, void* obj, void** args)
    {
        Inv r;
        if (!m)
        {
            r.err = "null method";
            return r;
        }
        MonoObject* exc = nullptr;
        r.ret = mono_runtime_invoke(m, obj, args, &exc);
        if (exc)
        {
            r.err = ExcText(exc);
            r.ret = nullptr;
            return r;
        }
        r.ok = true;
        return r;
    }

    MonoClass* ClassOf(MonoObject* obj)
    {
        return (obj && mono_object_get_class) ? mono_object_get_class(obj) : nullptr;
    }

    // 在单个类上找方法：先走 mono 的按名查表，再枚举（覆盖显式接口实现
    // "Ns.IFace.Method" 这种按全名落表、按短名查不到的情况），最后用签名
    // 参数个数过滤重载。
    static MonoMethod* FindMethodIn(MonoClass* cls, const char* name, int argc)
    {
        if (!cls)
            return nullptr;
        if (mono_class_get_method_from_name)
        {
            MonoMethod* m = mono_class_get_method_from_name(cls, name, argc);
            if (m)
                return m;
        }
        if (!mono_class_get_methods || !mono_method_get_name)
            return nullptr;
        void* it = nullptr;
        while (MonoMethod* m = (MonoMethod*)mono_class_get_methods(cls, &it))
        {
            const char* mn = mono_method_get_name(m);
            if (!mn)
                continue;
            const char* dot = strrchr(mn, '.');
            const char* sn = dot ? dot + 1 : mn;
            if (strcmp(sn, name) != 0)
                continue;
            if (argc >= 0 && mono_method_signature && mono_signature_get_param_count)
            {
                void* sig = mono_method_signature(m);
                if (sig && (int)mono_signature_get_param_count(sig) != argc)
                    continue;
            }
            return m;
        }
        return nullptr;
    }

    MonoMethod* FindMethod(MonoClass* cls, const char* name, int argc)
    {
        MonoClass* c = cls;
        for (int depth = 0; c && depth < 12; depth++)
        {
            if (MonoMethod* m = FindMethodIn(c, name, argc))
                return m;
            if (mono_class_get_interfaces)
            {
                void* it = nullptr;
                while (MonoClass* iface = (MonoClass*)mono_class_get_interfaces(c, &it))
                {
                    if (MonoMethod* m = FindMethodIn(iface, name, argc))
                        return m;
                }
            }
            c = mono_class_get_parent ? mono_class_get_parent(c) : nullptr;
        }
        return nullptr;
    }

    Inv InvokeByName(MonoClass* cls, void* obj, const char* name, int argc, void** args)
    {
        Inv r;
        if (!cls)
        {
            r.err = "null class";
            return r;
        }
        MonoMethod* m = FindMethod(cls, name, argc);
        if (!m)
        {
            r.err = std::string("method not found: ") + name;
            return r;
        }
        return Invoke(m, obj, args);
    }

    Inv InvokeStatic(MonoClass* cls, const char* name, int argc, void** args)
    {
        return InvokeByName(cls, nullptr, name, argc, args);
    }

    Inv PropGet(MonoClass* cls, void* obj, const char* name, void** idx)
    {
        Inv r;
        if (!cls)
        {
            r.err = "null class";
            return r;
        }
        // 沿「类 → 接口 → 父类」找属性声明，命中后走 mono_property_get_value
        MonoClass* c = cls;
        for (int depth = 0; c && depth < 12; depth++)
        {
            if (mono_class_get_property_from_name && mono_property_get_value)
            {
                MonoProperty* p = mono_class_get_property_from_name(c, name);
                if (p)
                {
                    MonoObject* exc = nullptr;
                    r.ret = mono_property_get_value(p, obj, idx, &exc);
                    if (exc)
                    {
                        r.err = ExcText(exc);
                        return r;
                    }
                    r.ok = true;
                    return r;
                }
            }
            if (mono_class_get_interfaces)
            {
                void* it = nullptr;
                while (MonoClass* iface = (MonoClass*)mono_class_get_interfaces(c, &it))
                {
                    if (!mono_class_get_property_from_name)
                        break;
                    MonoProperty* p = mono_class_get_property_from_name(iface, name);
                    if (p && mono_property_get_value)
                    {
                        MonoObject* exc = nullptr;
                        r.ret = mono_property_get_value(p, obj, idx, &exc);
                        if (exc)
                        {
                            r.err = ExcText(exc);
                            return r;
                        }
                        r.ok = true;
                        return r;
                    }
                }
            }
            c = mono_class_get_parent ? mono_class_get_parent(c) : nullptr;
        }
        // 兜底：get_<name>() 方法
        std::string getter = std::string("get_") + name;
        MonoMethod* m = FindMethod(cls, getter.c_str(), 0);
        if (m)
            return Invoke(m, obj, nullptr);
        r.err = std::string("property not found: ") + name;
        return r;
    }

    Inv PropSet(MonoClass* cls, void* obj, const char* name, void** args)
    {
        Inv r;
        if (!cls || !mono_class_get_property_from_name)
        {
            r.err = "property unsupported";
            return r;
        }
        MonoProperty* p = mono_class_get_property_from_name(cls, name);
        if (!p)
        {
            r.err = std::string("property not found: ") + name;
            return r;
        }
        MonoObject* exc = nullptr;
        mono_property_set_value(p, obj, args, &exc);
        if (exc)
        {
            r.err = ExcText(exc);
            return r;
        }
        r.ok = true;
        return r;
    }

    // ---------- 泛型 ----------
    MonoMethod* InflateGeneric(MonoClass* declClass, const char* name, int argc, MonoClass* typeArg)
    {
        if (!declClass || !mono_method_get_object || !mono_type_get_object || !mono_class_get_type)
            return nullptr;
        MonoDomain* dom = Dom();
        if (!dom)
            return nullptr;

        MonoMethod* def = mono_class_get_method_from_name(declClass, name, argc);
        if (!def)
        {
            Log::Printf("[MonoHost] generic def not found: %s", name);
            return nullptr;
        }
        MonoObject* info = mono_method_get_object(dom, def, declClass);
        if (!info)
        {
            Log::Printf("[MonoHost] MethodInfo create failed: %s", name);
            return nullptr;
        }

        // MethodInfo.MakeGenericMethod(Type[])（反射调用，参数需装箱）
        MonoMethod* mgm = mono_class_get_method_from_name(s_type ? mono_object_get_class(info) : nullptr,
                                                          "MakeGenericMethod", 1);
        if (!mgm)
            mgm = mono_class_get_method_from_name(s_type, "MakeGenericMethod", 1);
        if (!mgm)
        {
            Log::Printf("[MonoHost] MakeGenericMethod not found");
            return nullptr;
        }

        MonoObject* typeObj = mono_type_get_object(dom, mono_class_get_type(typeArg));
        if (!typeObj)
            return nullptr;
        MonoArray* types = ObjArrNew(1);
        if (!types)
            return nullptr;
        ObjArrSet(types, 0, typeObj);

        void* args[1] = { types };
        MonoObject* exc = nullptr;
        MonoObject* closed = mono_runtime_invoke(mgm, info, args, &exc);
        if (exc || !closed)
        {
            Log::Printf("[MonoHost] MakeGenericMethod failed: %s",
                        exc ? ExcText(exc).c_str() : "null");
            return nullptr;
        }

        // 从 MethodInfo 对象里取膨胀后的 MonoMethod*（MonoMethod.method 字段），
        // 之后走原始调用通道，免去反射 Invoke 的全参装箱
        MonoClass* mc = mono_object_get_class(closed);
        MonoClassField* f = mono_class_get_field_from_name(mc, "method");
        if (!f)
        {
            Log::Printf("[MonoHost] MonoMethod.method field not found");
            return nullptr;
        }
        MonoMethod* inflated = nullptr;
        mono_field_get_value(closed, f, &inflated);
        return inflated;
    }

    // 托管通道：MakeGenericMethod 后用 MethodInfo.Invoke(object, object[]) 调用。
    // 由 mono 自己处理膨胀方法的 JIT 与参数封送，绕开"开放泛型被直接 JIT"的运行时断言。
    Inv InvokeGenericManaged(MonoClass* cls, const char* name, int argc, MonoClass* typeArg, void* obj, void** args)
    {
        Inv r;
        if (!cls || !mono_method_get_object || !mono_type_get_object || !mono_class_get_type)
        {
            r.err = "mono api missing";
            return r;
        }
        MonoDomain* dom = Dom();
        MonoMethod* def = mono_class_get_method_from_name(cls, name, argc);
        if (!def)
        {
            r.err = std::string("generic def not found: ") + name;
            return r;
        }
        MonoObject* info = mono_method_get_object(dom, def, cls);
        if (!info)
        {
            r.err = "MethodInfo create failed";
            return r;
        }
        // MakeGenericMethod(Type[])：声明在 System.Reflection.MethodInfo 上
        // （System.Type 是 MemberInfo 的另一分支，从 Type 上找不到）
        MonoMethod* mgm = mono_class_get_method_from_name(mono_object_get_class(info),
                                                          "MakeGenericMethod", 1);
        if (!mgm)
            mgm = mono_class_get_method_from_name(s_type, "MakeGenericMethod", 1);
        if (!mgm)
        {
            r.err = "MakeGenericMethod not found";
            return r;
        }
        MonoObject* typeObj = mono_type_get_object(dom, mono_class_get_type(typeArg));
        MonoArray* types = ObjArrNew(1);
        if (!typeObj || !types)
        {
            r.err = "type array failed";
            return r;
        }
        ObjArrSet(types, 0, typeObj);
        void* mgArgs[1] = { types };
        MonoObject* exc = nullptr;
        MonoObject* closed = mono_runtime_invoke(mgm, info, mgArgs, &exc);
        if (exc || !closed)
        {
            r.err = exc ? ExcText(exc) : "MakeGenericMethod null";
            return r;
        }
        // MethodInfo.Invoke(object obj, object[] parameters)（MethodBase 声明）
        static MonoClass* s_methodBase = nullptr;
        if (!s_methodBase)
            s_methodBase = mono_class_from_name(mono_image_loaded("mscorlib"),
                                                "System.Reflection", "MethodBase");
        MonoMethod* inv = s_methodBase ? mono_class_get_method_from_name(s_methodBase, "Invoke", 2) : nullptr;
        if (!inv)
        {
            r.err = "MethodInfo.Invoke not found";
            return r;
        }
        MonoArray* margs = ObjArrNew(argc > 0 ? argc : 0);
        if (!margs)
        {
            r.err = "args array failed";
            return r;
        }
        // 原生 args → object[]（本工程托管泛型调用的参数均为引用类型，直接放指针）
        for (int i = 0; i < argc; i++)
            ObjArrSet(margs, i, (MonoObject*)args[i]);
        void* invArgs[2] = { obj, margs };
        MonoObject* exc2 = nullptr;
        r.ret = mono_runtime_invoke(inv, closed, invArgs, &exc2);
        if (exc2)
        {
            r.err = ExcText(exc2);
            r.ret = nullptr;
            return r;
        }
        r.ok = true;
        return r;
    }

    Inv InvokeGenericStatic(MonoClass* cls, const char* name, MonoClass* typeArg, int argc, void** args)
    {
        Inv r;
        MonoMethod* m = InflateGeneric(cls, name, argc, typeArg);
        if (!m)
        {
            r.err = std::string("inflate failed: ") + name;
            return r;
        }
        return Invoke(m, nullptr, args);
    }

    Inv InvokeGenericInstance(MonoClass* cls, void* obj, const char* name, MonoClass* typeArg, int argc, void** args)
    {
        Inv r;
        MonoMethod* m = InflateGeneric(cls, name, argc, typeArg);
        if (!m)
        {
            r.err = std::string("inflate failed: ") + name;
            return r;
        }
        return Invoke(m, obj, args);
    }

    // ---------- 对象 / 装箱 ----------
    MonoObject* New(MonoClass* cls)
    {
        // Activator.CreateInstance(Type)
        static MonoClass* s_activator = nullptr;
        if (!s_activator)
            s_activator = mono_class_from_name(mono_image_loaded("mscorlib"), "System", "Activator");
        MonoObject* t = TypeOf(cls);
        if (!s_activator || !t)
            return nullptr;
        void* args[1] = { t };
        return InvokeStatic(s_activator, "CreateInstance", 1, args).ret;
    }

    MonoObject* TypeOf(MonoClass* cls)
    {
        MonoDomain* dom = Dom();
        if (!dom || !cls || !mono_class_get_type || !mono_type_get_object)
            return nullptr;
        return mono_type_get_object(dom, mono_class_get_type(cls));
    }

    MonoObject* Box(MonoClass* cls, void* raw)
    {
        MonoDomain* dom = Dom();
        if (!dom || !cls || !raw || !mono_value_box)
            return nullptr;
        return mono_value_box(dom, cls, raw);
    }

    MonoObject* BoxInt(std::int32_t v)  { return Box(s_int32, &v); }

    void* RawArg(MonoObject* boxed)
    {
        return (boxed && mono_object_unbox) ? mono_object_unbox(boxed) : nullptr;
    }
    MonoObject* BoxI64(std::int64_t v)  { return Box(s_int64, &v); }
    MonoObject* BoxBool(bool v)         { return Box(s_boolean, &v); }
    MonoObject* BoxFloat(float v)       { return Box(s_single, &v); }

    MonoObject* EnumVal(MonoClass* enumCls, int v)
    {
        MonoDomain* dom = Dom();
        if (!dom || !enumCls || !mono_object_new)
            return nullptr;
        MonoObject* o = mono_object_new(dom, enumCls);
        if (!o)
            return nullptr;
        *(int*)mono_object_unbox(o) = v;
        return o;
    }

    MonoObject* MakeGenericType(MonoObject* openType, MonoClass* arg)
    {
        if (!openType || !arg || !s_type)
            return nullptr;
        MonoMethod* m = mono_class_get_method_from_name(s_type, "MakeGenericType", 1);
        if (!m)
            return nullptr;
        MonoArray* types = ObjArrNew(1);
        if (!types)
            return nullptr;
        ObjArrSet(types, 0, TypeOf(arg));
        void* args[1] = { types };
        return Invoke(m, openType, args).ret;
    }

    // ---------- 数组 ----------
    MonoArray* ObjArrNew(int n)
    {
        MonoDomain* dom = Dom();
        if (!dom || !s_object || !mono_array_new)
            return nullptr;
        return mono_array_new(dom, s_object, (uintptr_t)n);
    }

    void ObjArrSet(MonoArray* arr, int i, MonoObject* v)
    {
        if (!arr || !mono_array_addr_with_size)
            return;
        *(MonoObject**)mono_array_addr_with_size((MonoObject*)arr, sizeof(void*), i) = v;
    }

    MonoObject* ObjArrGet(MonoArray* arr, int i)
    {
        if (!arr || !mono_array_addr_with_size)
            return nullptr;
        return *(MonoObject**)mono_array_addr_with_size((MonoObject*)arr, sizeof(void*), i);
    }

    MonoArray* PrimArrNew(MonoClass* elemClass, int n)
    {
        MonoDomain* dom = Dom();
        if (!dom || !elemClass || !mono_array_new)
            return nullptr;
        return mono_array_new(dom, elemClass, (uintptr_t)n);
    }

    // ---------- 快捷 ----------
    MonoClass* ClassString()  { return s_string; }
    MonoClass* ClassType()    { return s_type; }
    MonoClass* ClassObject()  { return s_object; }
    MonoClass* ClassSingle()  { return s_single; }
    MonoClass* ClassInt32()   { return s_int32; }
    MonoClass* ClassInt64()   { return s_int64; }
    MonoClass* ClassBoolean() { return s_boolean; }
}
