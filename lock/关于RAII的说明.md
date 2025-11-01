**RAII 风格**（Resource Acquisition Is Initialization，直译为“资源获取即初始化”）。

---

## 1. **RAII 的基本思想**

* 在 C++ 中，**对象的生命周期由构造函数和析构函数管理**。
* RAII 就是把 **资源的获取与释放** 放到对象的 **构造函数** 和 **析构函数** 里，让编译器自动帮我们管理资源。

换句话说：

* **构造函数负责获取资源**（打开文件、申请内存、初始化锁等）；
* **析构函数负责释放资源**（关闭文件、释放内存、销毁锁等）。

这样一来：

* 当对象被创建时，资源就自动分配好了。
* 当对象生命周期结束时（超出作用域 / 被销毁），资源就自动释放了。

你不用担心忘记 `free`、`close`、`unlock`，也不用在异常时手动清理。

---

## 2. **举个例子**

### 没有 RAII 的写法

```cpp
void foo() {
    FILE* f = fopen("data.txt", "r");
    if (!f) return;

    // ... 使用文件 ...

    fclose(f); // ⚠️ 如果这里有异常或提前 return，可能忘记关闭文件
}
```

这里如果函数中途 `return` 或者抛出异常，就可能忘记调用 `fclose`，造成 **资源泄漏**。

---

### 使用 RAII

```cpp
class File {
public:
    File(const char* name, const char* mode) {
        f = fopen(name, mode);
        if (!f) throw std::runtime_error("open file failed");
    }
    ~File() {
        if (f) fclose(f);
    }
    FILE* get() { return f; }
private:
    FILE* f;
};

void foo() {
    File file("data.txt", "r"); // 构造函数自动打开
    // ... 使用 file.get() ...
} // 作用域结束，析构函数自动 fclose
```

👉 这样无论函数怎么退出，文件都会自动关闭。

---

## 3. **典型 RAII 应用**

RAII 在 C++ 中非常常见，尤其是 STL 和并发编程里：

* **`std::lock_guard<std::mutex>`**
  自动加锁/解锁，避免死锁。
* **`std::unique_ptr`、`std::shared_ptr`**
  自动管理动态内存，避免 `new/delete` 泄漏。
* **`std::fstream`**
  构造函数打开文件，析构函数关闭文件。

---

## 4. **RAII 的优势**

1. 让代码更安全，避免忘记释放资源。
2. 异常安全：即使抛异常，析构函数仍然会执行，保证资源释放。
3. 简化代码，减少样板式的 `malloc/free`、`lock/unlock`。

---

✅ 总结一句话：
RAII 就是 **把资源交给对象管理**，让“构造即获取，析构即释放”。这样能确保资源不会泄漏，也让代码更优雅。
