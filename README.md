# SwapAddSubPass

Этот LLVM пасс модифицирует LLVM IR во время компиляции. Он находит все функции,
имя которых содержит "replace". Для этих функций и всех функций, вызываемых
ими (прямо или косвенно), все целочисленные инструкции сложения (`add`)
заменяются на вычитание (`sub`), а вычитание (`sub`) на сложение (`add`). Меняются бинарные `+` и `-`, а также `++` и `--`, так как на этапе применения пасса они неразличимы.


## Требования

*   **ОС:** Протестировано на Ubuntu 22.04.5
*   **CMake:** Версия 3.13.4 или новее
*   **Компилятор C++:** Clang 17.0.6
*   **Ninja:**

## Окружение Сборки

*   **LLVM:** Ветка `release/17.x` из репозитория `llvm-project`
*   **Структура директорий:**
    ```
    <родительская_папка>/
    ├── llvm-project/
    │   ├── build/
    │   └── llvm/      # Исходники LLVM
    │   └── clang/     # Исходники Clang
    │   └── ...
    └── SwapAddSubPass/
        ├── build/
        ├── SwapAddSubPass.cpp
        ├── CMakeLists.txt
        └── test.cpp
        └── ...
    ```

## Шаг 1: Сборка LLVM

1.  **Клонируйте репозиторий LLVM:**
    ```bash
    git clone https://github.com/llvm/llvm-project.git
    cd llvm-project
    git checkout release/17.x
    ```

2.  **Создайте директорию для сборки LLVM и перейдите в нее:**
    ```bash
    mkdir build
    cd build
    ```

3.  **Настройте сборку LLVM с помощью CMake:**
    ```bash
    cmake -G Ninja ../llvm -DLLVM_BUILD_LLVM_DYLIB=ON -DLLVM_LINK_LLVM_DYLIB=ON -DLLVM_ENABLE_PROJECTS="clang" -DCMAKE_BUILD_TYPE=Release
    ```

4.  **Запустите сборку LLVM:**
    ```bash
    ninja
    ```

## Шаг 2: Сборка Пасса

1.  **Перейдите в директорию пасса:**
    ```bash
    cd ../../SwapAddSubPass
    ```

2.  **Создайте директорию для сборки пасса и перейдите в нее:**
    ```bash
    mkdir build
    cd build
    ```

3.  **Настройте сборку пасса с помощью CMake:**
    ```bash
    cmake .. -DCMAKE_CXX_COMPILER=../../llvm-project/build/bin/clang++
    ```

4.  **Запустите сборку пасса:**
    ```bash
    make
    ```

## Шаг 3: Тестирование Пасса

Выполняется из папки `SwapAddSubPass`, где находится файл `test.cpp`.

1.  **Компиляция `test.cpp` в LLVM IR:**
    ```bash
    ../llvm-project/build/bin/clang++ -S -emit-llvm test.cpp -o test.ll
    ```

2.  **Применение пасса к LLVM IR:**
    ```bash
    ../llvm-project/build/bin/opt -load-pass-plugin=./build/libSwapAddSub.so -passes=swap-add-sub test.ll -o modified.ll
    ```

3.  **Компиляция модифицированного LLVM IR в объектный файл:**
    ```bash
    ../llvm-project/build/bin/llc -filetype=obj -relocation-model=pic modified.ll -o modified.o
    ```

4.  **Линковка объектного файла в исполняемый файл:**
    ```bash
    ../llvm-project/build/bin/clang++ -pie modified.o -o test
    ```

5.  **Запуск исполняемого файла:**
    ```bash
    ./test
    ```
    Вывод програамы: `51-1`

    ![Вывод программы](https://github.com/user-attachments/assets/69c0834e-4a3a-4ddb-9028-47b67b11b314)
