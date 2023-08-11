<div align="center">
<h2>Concurrent Partial Mark And Sweep</h2>
<p>並行循環参照コレクタ</p>
</div>

[開発中のプログラミング言語(Catla)](https://github.com/bea4dev/catla)で使用(不完全な実装)しているStop The Worldを引き起こさない循環参照コレクタアルゴリズム [^1] [^2]

[^1]: 参考文献 『ガベージコレクション 自動的メモリ管理を構成する理論と実装』
[^2]: 参考文献 https://pages.cs.wisc.edu/~cymen/misc/interests/Bacon01Concurrent.pdf

アルゴリズムの詳細は[src/cycle_collector.cpp](https://github.com/bea4dev/ConcurrentPartialMarkAndSweep/blob/main/src/cycle_collector.cpp)に記述されている。
実装は[動的変異参照カウントの実装](https://github.com/bea4dev/DynamicMutationReferenceCounting)に追記したものである。

## テストプログラムのビルド方法
ビルドには g++ or clang++, cmake, git が必要です。

1. https://github.com/google/benchmark のプロジェクトをビルドしてインストールします。
下記の手順は上記サイトの引用です。
```bash
# Check out the library.
$ git clone https://github.com/google/benchmark.git
# Go to the library root directory
$ cd benchmark
# Make a build directory to place the build output.
$ cmake -E make_directory "build"
# Generate build system files with cmake, and download any dependencies.
$ cmake -E chdir "build" cmake -DBENCHMARK_DOWNLOAD_DEPENDENCIES=on -DCMAKE_BUILD_TYPE=Release ../
# or, starting with CMake 3.13, use a simpler form:
# cmake -DCMAKE_BUILD_TYPE=Release -S . -B "build"
# Build the library.
$ cmake --build "build" --config Release

# Install
$ sudo cmake --build "build" --config Release --target install
```
2. このプロジェクトを clone してビルドします。
```bash
# Clone this project.
$ git clone https://github.com/bea4dev/ConcurrentPartialMarkAndSweep.git
# Go to the project root.
$ cd ConcurrentPartialMarkAndSweep
# Build this project.
$ cmake -S . -B build
$ cmake --build build
```
3. 実行
```bash
$ ./build/dynamic_rc_benchmark
```