#pragma once
// ============================================================
// MonoHost.h — 托管世界桥（仅游戏主线程调用）。
// 在 mono C API 之上封装反射调用、对象构造、泛型方法展开、
// 字符串/数组/枚举/装箱 等高频操作。所有失败路径返回 ok=false，
// 绝不让托管异常泄漏成原生崩溃。
// ============================================================
#include "MonoApi.h"
#include <string>
#include <cstdint>

namespace MonoHost
{
    // 主线程一次性初始化（解析 System.Type / Activator / List`1 等核心类）
    bool Init();

    // ---------- 镜像 / 类 ----------
    MonoImage* Image(const char* name);                       // 已加载镜像（如 "App"）
    MonoClass* Class(MonoImage* img, const char* ns, const char* name);
    std::string ClassName(MonoClass* cls);
    MonoClass* ClassOf(MonoObject* obj);                      // 运行期实例的类

    // ---------- 调用结果 ----------
    struct Inv
    {
        MonoObject* ret = nullptr;
        bool ok = false;
        std::string err;
    };

    // ---------- 字符串 ----------
    MonoObject*  Str(const char* utf8);                       // utf8 → System.String
    MonoObject*  StrW(const std::wstring& w);
    std::string  StrUtf8(MonoObject* str);                    // System.String → utf8
    std::wstring StrWide(MonoObject* str);

    // ---------- 调用 ----------
    Inv Invoke(MonoMethod* m, void* obj, void** args);        // 原始 mono 调用 + 异常捕获
    Inv InvokeByName(MonoClass* cls, void* obj, const char* name, int argc, void** args);
    // 沿「类 → 父类 → 接口」链查找方法；能命中显式接口实现（"Ns.Iface.Name"）。
    // 便于在拿不到接口 MonoClass* 时，直接用实例类调用。
    MonoMethod* FindMethod(MonoClass* cls, const char* name, int argc);
    // 精确重载查找：名称 + 参数个数 + 首参类型全名（如 "System.Type"）。
    // 自动跳过开放泛型（mono_class_get_method_from_name 只按名字+参数个数匹配，
    // 会误中同名泛型定义 → JIT method-to-ir.c:6288 致命断言）。
    MonoMethod* FindMethodExact(MonoClass* cls, const char* name, int argc, const char* firstParamType);
    Inv InvokeStatic(MonoClass* cls, const char* name, int argc, void** args);
    Inv PropGet(MonoClass* cls, void* obj, const char* name, void** idx = nullptr);
    Inv PropSet(MonoClass* cls, void* obj, const char* name, void** args);

    // 泛型方法：MethodInfo 展开 → 走托管 MethodInfo.Invoke（装箱参数；稳定路线）
    MonoMethod* InflateGeneric(MonoClass* declClass, const char* name, int argc, MonoClass* typeArg);   // 原生通道（慎用：开放泛型 JIT 断言风险）
    Inv InvokeGenericManaged(MonoClass* cls, const char* name, int argc, MonoClass* typeArg, void* obj, void** args);
    Inv InvokeGenericStatic(MonoClass* cls, const char* name, MonoClass* typeArg, int argc, void** args);
    Inv InvokeGenericInstance(MonoClass* cls, void* obj, const char* name, MonoClass* typeArg, int argc, void** args);

    // ---------- 对象 / 装箱 ----------
    MonoObject* New(MonoClass* cls);                          // Activator.CreateInstance(Type)
    MonoObject* TypeOf(MonoClass* cls);
    MonoObject* Box(MonoClass* cls, void* raw);               // mono_value_box
    MonoObject* BoxInt(std::int32_t v);
    MonoObject* BoxI64(std::int64_t v);
    MonoObject* BoxBool(bool v);
    MonoObject* BoxFloat(float v);
    MonoObject* EnumVal(MonoClass* enumCls, int v);

    // ---------- Type 操作 ----------
    // ---------- raw args (mono_runtime_invoke convention) ----------
    //   Value-type params (int/bool/float/...) must be passed as a pointer
    //   to the raw value, NOT as a boxed object pointer. Passing the boxed
    //   object makes the callee read the object header as the value, which
    //   produced System.ArgumentException on AudioClip.Create.
    void* RawArg(MonoObject* boxed);

    MonoObject* MakeGenericType(MonoObject* openType, MonoClass* arg); // Type.MakeGenericType(Type[])

    // ---------- 数组 ----------
    MonoArray*  ObjArrNew(int n);
    void        ObjArrSet(MonoArray* arr, int i, MonoObject* v);
    MonoObject* ObjArrGet(MonoArray* arr, int i);
    MonoArray*  PrimArrNew(MonoClass* elemClass, int n);

    // ---------- 快捷：类缓存 ----------
    MonoClass* ClassString();
    MonoClass* ClassType();          // System.Type
    MonoClass* ClassObject();
    MonoClass* ClassSingle();        // System.Single
    MonoClass* ClassInt32();
    MonoClass* ClassInt64();
    MonoClass* ClassBoolean();
}
