#include "manual_object.hpp"
#include "dynamic_rc.hpp"
#include "single_thread_rc.hpp"
#include "thread_safe_rc.hpp"
#include <iostream>
#include <vector>
#include <thread>
#include <benchmark/benchmark.h>
#include <functional>

//全オブジェクトのフィールドの長さ
#define OBJECT_FIELD_LENGTH 2

//マルチスレッドベンチマークに使用するスレッド数
#define NUMBER_OF_THREADS 8


#if RC_VALIDATION
    atomic_size_t global_object_count;
#endif


/**
 * 指定された型で木構造オブジェクトを作成
 */
template<typename T> T create_tree(size_t count, size_t tree_depth);


/**
 * シングルスレッドで木構造オブジェクトを作成するベンチマーク用関数
 * メモリ管理方法 : 手動
 */
static void benchmark_single_thread_manual_object(benchmark::State& state);

/**
 * シングルスレッドで木構造オブジェクトを作成するベンチマーク用関数
 * メモリ管理方法 : シングススレッド専用参照カウント
 */
static void benchmark_single_thread_single_thread_rc(benchmark::State& state);

/**
 * シングルスレッドで木構造オブジェクトを作成するベンチマーク用関数
 * メモリ管理方法 : スレッドセーフな参照カウント
 */
static void benchmark_single_thread_thread_safe_rc(benchmark::State& state);

/**
 * シングルスレッドで木構造オブジェクトを作成するベンチマーク用関数
 * メモリ管理方法 : 動的切り替え参照カウント
 */
static void benchmark_single_thread_dynamic_rc(benchmark::State& state);

/**
 * マルチスレッドで木構造オブジェクトを作成するベンチマーク用関数
 * メモリ管理方法 : スレッドセーフな参照カウント
 */
static void benchmark_multi_thread_thread_safe_rc(benchmark::State& state);

/**
 * マルチスレッドで木構造オブジェクトを作成するベンチマーク用関数
 * メモリ管理方法 : 動的切り替え参照カウント
 */
static void benchmark_multi_thread_dynamic_rc(benchmark::State& state);

/**
 * 循環参照コレクタの速度評価用ベンチマーク (非GC時)
 */
static void benchmark_multithread_with_non_gc(benchmark::State& state);

/**
 * 循環参照コレクタの速度評価用ベンチマーク (GC時)
 */
static void benchmark_multithread_with_gc(benchmark::State& state);


//各種ベンチマーク関数の登録
//詳細は以下を参照
//https://github.com/google/benchmark
BENCHMARK(benchmark_single_thread_manual_object);
BENCHMARK(benchmark_single_thread_single_thread_rc);
BENCHMARK(benchmark_single_thread_thread_safe_rc);
BENCHMARK(benchmark_single_thread_dynamic_rc);
BENCHMARK(benchmark_multi_thread_thread_safe_rc);
BENCHMARK(benchmark_multi_thread_dynamic_rc);
BENCHMARK(benchmark_multithread_with_non_gc);
BENCHMARK(benchmark_multithread_with_gc);

//複数のスレッドから直接アクセス可能なオブジェクト
ThreadSafeRC global_variable_with_thread_safe_rc(alloc_heap_object(OBJECT_FIELD_LENGTH));
DynamicRC global_variable_with_dynamic_rc(alloc_heap_object(10), true); //予め mutex としてマーク


size_t get_clock_time() {
    return clock() % 10;
}


#if RC_VALIDATION
int main() {
    //L74 - L75で作成したオブジェクトのカウントをリセット
    global_object_count.store(0, memory_order_relaxed);

    //木構造オブジェクトの作成と削除(手動)
    { create_tree<ManualObject>(0, 25).detele_object(); }

    //木構造オブジェクトの作成と削除(シングルスレッド専用参照カウント)
    { create_tree<SingleThreadRC>(0, 25); }

    //木構造オブジェクトの作成と削除(スレッドセーフな参照カウント)
    { create_tree<ThreadSafeRC>(0, 25); }

    //木構造オブジェクトの作成と削除(動的切り替え参照カウント)
    { create_tree<DynamicRC>(0, 25); }

    {//マルチスレッドで木構造オブジェクトを作成する(スレッドセーフな参照カウント)
        auto func = []() {
            for (size_t i = 0; i < 100; i++) {
                //木構造オブジェクトを作成
                auto tree = create_tree<ThreadSafeRC>(0, 10);
                //グローバル変数へ渡す
                global_variable_with_thread_safe_rc.set_object(0, tree);
            }
        };
        vector<thread> threads;
        //スレッド起動
        for (size_t i = 0; i < NUMBER_OF_THREADS; i++) {
            threads.push_back(thread(func));
        }
        //スレッド終了待機
        for (auto it = threads.begin(); it != threads.end(); ++it) {
            it->join();
        }
        //グローバル変数へ挿入されているオブジェクトを削除
        global_variable_with_thread_safe_rc.set_object(0, nullopt);
    }

    {//マルチスレッドで木構造オブジェクトを作成する(動的切り替え参照カウント)
        auto func = []() {
            for (size_t i = 0; i < 100; i++) {
                //木構造オブジェクトを作成
                auto tree = create_tree<DynamicRC>(0, 10);
                //グローバル変数へ渡す
                global_variable_with_dynamic_rc.set_object(0, tree);
            }
        };
        vector<thread> threads;
        //スレッド起動
        for (size_t i = 0; i < NUMBER_OF_THREADS; i++) {
            threads.push_back(thread(func));
        }
        //スレッド終了待機
        for (auto it = threads.begin(); it != threads.end(); ++it) {
            it->join();
        }
        //グローバル変数へ挿入されているオブジェクトを削除
        global_variable_with_dynamic_rc.set_object(0, nullopt);
    }

    {
        //予め全てのフィールドにオブジェクトをセット
        for (size_t i = 0; i < 10; i++) {
            DynamicRC object(alloc_heap_object(OBJECT_FIELD_LENGTH));
            object.mark_as_cyclic_type();
            global_variable_with_dynamic_rc.set_object(i, object);
        }

        //gc threadを停止するかどうか
        atomic_bool is_finished(false);

        //実行スレッド側の処理
        auto mutator_func = [](atomic_bool& is_finished) {
            for (size_t s = 0; s < 100000; s++) {
                //適度に循環参照を作成する
                if (get_clock_time() % 2 == 0) {
                    DynamicRC obj1(alloc_heap_object(OBJECT_FIELD_LENGTH));
                    DynamicRC obj2(alloc_heap_object(OBJECT_FIELD_LENGTH));
                    DynamicRC obj3(alloc_heap_object(OBJECT_FIELD_LENGTH));
                    obj1.mark_as_cyclic_type();
                    obj2.mark_as_cyclic_type();
                    obj3.mark_as_cyclic_type();
                    
                    global_variable_with_dynamic_rc.set_object(get_clock_time(), obj1);
                    global_variable_with_dynamic_rc.set_object(get_clock_time(), obj2);
                    global_variable_with_dynamic_rc.set_object(get_clock_time(), obj3);
                } else {
                    auto obj1 = global_variable_with_dynamic_rc.get_object(get_clock_time()).value();
                    auto obj2 = global_variable_with_dynamic_rc.get_object(get_clock_time()).value();
                    auto obj3 = global_variable_with_dynamic_rc.get_object(get_clock_time()).value();

                    if (get_clock_time() % 2 == 0) {
                        obj1.set_object(get_clock_time() % 2, obj2);
                        obj2.set_object(get_clock_time() % 2, obj3);
                    } else {
                        obj1.set_object(get_clock_time() % 2, obj2);
                        obj2.set_object(get_clock_time() % 2, obj3);
                        obj3.set_object(get_clock_time() % 2, obj1);
                    }
                }
            }

            //gc threadへ向けて終了シグナルを送信
            is_finished.store(true, memory_order_relaxed);
        };

        vector<thread> threads;
        //スレッド起動
        for (size_t i = 0; i < NUMBER_OF_THREADS - 1; i++) {
            threads.push_back(thread(mutator_func, ref(is_finished)));
        }

        //gc thread 側の処理
        auto gc_func = [](atomic_bool& is_finished) {
            //終了シグナルが送信されるまで、gcを走らせ続ける
            while (!is_finished.load(memory_order_relaxed)) {
                gc_collect();
            }
        };
        threads.push_back(thread(gc_func, ref(is_finished)));

        //スレッド終了待機
        for (auto it = threads.begin(); it != threads.end(); ++it) {
            it->join();
        }

        //グローバル変数へ挿入されているオブジェクトを削除
        for (size_t i = 0; i < 10; i++) {
            global_variable_with_dynamic_rc.set_object(i, nullopt);
        }
        
        //一度で全て回収しきれないことがあるので何度も呼び出す
        cout << "start collect" << endl;
        gc_collect();
        gc_collect();
        gc_collect();
        gc_collect();
        gc_collect();
        cout << "end collect" << endl;
    }

    //現在生存しているオブジェクト数を表示(0以外は不正)
    cout << "Global object count : " << global_object_count.load(memory_order_relaxed) << endl;

    return 0;
}
#else
//ベンチマークを走らせる
//詳細は以下を参照
//https://github.com/google/benchmark
BENCHMARK_MAIN();
#endif



/**
 * 指定された型で木構造オブジェクトを作成
 */
template<typename T> T create_tree(size_t count, size_t tree_depth) {
    auto* object_ref = alloc_heap_object(OBJECT_FIELD_LENGTH);

    T object(object_ref);

    if (count == tree_depth) {
        return object;
    }

    for (size_t i = 0; i < OBJECT_FIELD_LENGTH; i++) {
        auto child = create_tree<T>(count + 1, tree_depth);
        object.set_object(i, child);
    }

    return object;
}

/**
 * シングルスレッドで木構造オブジェクトを作成するベンチマーク用関数
 * メモリ管理方法 : 手動
 */
static void benchmark_single_thread_manual_object(benchmark::State& state) {
    for (auto _ : state) {
        create_tree<ManualObject>(0, 10).detele_object();
    }
}

/**
 * シングルスレッドで木構造オブジェクトを作成するベンチマーク用関数
 * メモリ管理方法 : シングススレッド専用参照カウント
 */
static void benchmark_single_thread_single_thread_rc(benchmark::State& state) {
    for (auto _ : state) {
        create_tree<SingleThreadRC>(0, 10);
    }
}

/**
 * シングルスレッドで木構造オブジェクトを作成するベンチマーク用関数
 * メモリ管理方法 : スレッドセーフな参照カウント
 */
static void benchmark_single_thread_thread_safe_rc(benchmark::State& state) {
    for (auto _ : state) {
        create_tree<ThreadSafeRC>(0, 10);
    }
}

/**
 * シングルスレッドで木構造オブジェクトを作成するベンチマーク用関数
 * メモリ管理方法 : 動的切り替え参照カウント
 */
static void benchmark_single_thread_dynamic_rc(benchmark::State& state) {
    for (auto _ : state) {
        create_tree<DynamicRC>(0, 10);
    }
}

/**
 * マルチスレッドで木構造オブジェクトを作成するベンチマーク用関数
 * メモリ管理方法 : スレッドセーフな参照カウント
 */
static void benchmark_multi_thread_thread_safe_rc(benchmark::State& state) {
    for (auto _ : state) {
        auto func = []() {
            for (size_t i = 0; i < 5; i++) {
                //木構造オブジェクトを作成
                auto tree = create_tree<ThreadSafeRC>(0, 20);
                //グローバル変数へ渡す
                global_variable_with_thread_safe_rc.set_object(0, tree);
            }
        };

        vector<thread> threads;
        //スレッド起動
        for (size_t i = 0; i < NUMBER_OF_THREADS; i++) {
            threads.push_back(thread(func));
        }

        //スレッド終了待機
        for (auto it = threads.begin(); it != threads.end(); ++it) {
            it->join();
        }

        //グローバル変数へ挿入されているオブジェクトを削除
        global_variable_with_thread_safe_rc.set_object(0, nullopt);
    }
}

/**
 * マルチスレッドで木構造オブジェクトを作成するベンチマーク用関数
 * メモリ管理方法 : 動的切り替え参照カウント
 */
static void benchmark_multi_thread_dynamic_rc(benchmark::State& state) {
    for (auto _ : state) {
        auto func = []() {
            for (size_t i = 0; i < 5; i++) {
                //木構造オブジェクトを作成
                auto tree = create_tree<DynamicRC>(0, 20);
                //グローバル変数へ渡す
                //この時mutex化が起こる
                //詳細については"dynamic_rc.hpp"を参照
                global_variable_with_dynamic_rc.set_object(0, tree);
            }
        };

        vector<thread> threads;
        //スレッド起動
        for (size_t i = 0; i < NUMBER_OF_THREADS; i++) {
            threads.push_back(thread(func));
        }

        //スレッド終了待機
        for (auto it = threads.begin(); it != threads.end(); ++it) {
            it->join();
        }

        //グローバル変数へ挿入されているオブジェクトを削除
        global_variable_with_dynamic_rc.set_object(0, nullopt);
    }
}

/**
 * 循環参照コレクタの速度評価用ベンチマーク (非GC時)
 */
static void benchmark_multithread_with_non_gc(benchmark::State& state) {
    for (auto _ : state) {
        //予め全てのフィールドにオブジェクトをセット
        for (size_t i = 0; i < 10; i++) {
            DynamicRC object(alloc_heap_object(OBJECT_FIELD_LENGTH));
            object.mark_as_cyclic_type();
            global_variable_with_dynamic_rc.set_object(i, object);
        }

        //実行スレッド側の処理
        auto mutator_func = []() {
            for (size_t s = 0; s < 100000; s++) {
                //適度に循環参照を作成する
                if (get_clock_time() % 2 == 0) {
                    DynamicRC obj1(alloc_heap_object(OBJECT_FIELD_LENGTH));
                    DynamicRC obj2(alloc_heap_object(OBJECT_FIELD_LENGTH));
                    DynamicRC obj3(alloc_heap_object(OBJECT_FIELD_LENGTH));
                    obj1.mark_as_cyclic_type();
                    obj2.mark_as_cyclic_type();
                    obj3.mark_as_cyclic_type();
                    
                    global_variable_with_dynamic_rc.set_object(get_clock_time(), obj1);
                    global_variable_with_dynamic_rc.set_object(get_clock_time(), obj2);
                    global_variable_with_dynamic_rc.set_object(get_clock_time(), obj3);
                } else {
                    auto obj1 = global_variable_with_dynamic_rc.get_object(get_clock_time()).value();
                    auto obj2 = global_variable_with_dynamic_rc.get_object(get_clock_time()).value();
                    auto obj3 = global_variable_with_dynamic_rc.get_object(get_clock_time()).value();

                    if (get_clock_time() % 2 == 0) {
                        obj1.set_object(get_clock_time() % 2, obj2);
                        obj2.set_object(get_clock_time() % 2, obj3);
                    } else {
                        obj1.set_object(get_clock_time() % 2, obj2);
                        obj2.set_object(get_clock_time() % 2, obj3);
                        obj3.set_object(get_clock_time() % 2, obj1);
                    }
                }
            }
        };

        vector<thread> threads;
        //スレッド起動
        for (size_t i = 0; i < NUMBER_OF_THREADS - 1; i++) {
            threads.push_back(thread(mutator_func));
        }

        //スレッド終了待機
        for (auto it = threads.begin(); it != threads.end(); ++it) {
            it->join();
        }

        //グローバル変数へ挿入されているオブジェクトを削除
        for (size_t i = 0; i < 10; i++) {
            global_variable_with_dynamic_rc.set_object(i, nullopt);
        }
    }
}

/**
 * 循環参照コレクタの速度評価用ベンチマーク (GC時)
 */
static void benchmark_multithread_with_gc(benchmark::State& state) {
    for (auto _ : state) {
        //予め全てのフィールドにオブジェクトをセット
        for (size_t i = 0; i < 10; i++) {
            DynamicRC object(alloc_heap_object(OBJECT_FIELD_LENGTH));
            object.mark_as_cyclic_type();
            global_variable_with_dynamic_rc.set_object(i, object);
        }

        //gc threadを停止するかどうか
        atomic_bool is_finished(false);

        //実行スレッド側の処理
        auto mutator_func = [](atomic_bool& is_finished) {
            for (size_t s = 0; s < 100000; s++) {
                //適度に循環参照を作成する
                if (get_clock_time() % 2 == 0) {
                    DynamicRC obj1(alloc_heap_object(OBJECT_FIELD_LENGTH));
                    DynamicRC obj2(alloc_heap_object(OBJECT_FIELD_LENGTH));
                    DynamicRC obj3(alloc_heap_object(OBJECT_FIELD_LENGTH));
                    obj1.mark_as_cyclic_type();
                    obj2.mark_as_cyclic_type();
                    obj3.mark_as_cyclic_type();
                    
                    global_variable_with_dynamic_rc.set_object(get_clock_time(), obj1);
                    global_variable_with_dynamic_rc.set_object(get_clock_time(), obj2);
                    global_variable_with_dynamic_rc.set_object(get_clock_time(), obj3);
                } else {
                    auto obj1 = global_variable_with_dynamic_rc.get_object(get_clock_time()).value();
                    auto obj2 = global_variable_with_dynamic_rc.get_object(get_clock_time()).value();
                    auto obj3 = global_variable_with_dynamic_rc.get_object(get_clock_time()).value();

                    if (get_clock_time() % 2 == 0) {
                        obj1.set_object(get_clock_time() % 2, obj2);
                        obj2.set_object(get_clock_time() % 2, obj3);
                    } else {
                        obj1.set_object(get_clock_time() % 2, obj2);
                        obj2.set_object(get_clock_time() % 2, obj3);
                        obj3.set_object(get_clock_time() % 2, obj1);
                    }
                }
            }

            //gc threadへ向けて終了シグナルを送信
            is_finished.store(true, memory_order_relaxed);
        };

        vector<thread> threads;
        //スレッド起動
        for (size_t i = 0; i < NUMBER_OF_THREADS - 1; i++) {
            threads.push_back(thread(mutator_func, ref(is_finished)));
        }

        //gc thread 側の処理
        auto gc_func = [](atomic_bool& is_finished) {
            //終了シグナルが送信されるまで、gcを走らせ続ける
            while (!is_finished.load(memory_order_relaxed)) {
                gc_collect();
            }
        };
        threads.push_back(thread(gc_func, ref(is_finished)));

        //スレッド終了待機
        for (auto it = threads.begin(); it != threads.end(); ++it) {
            it->join();
        }

        //グローバル変数へ挿入されているオブジェクトを削除
        for (size_t i = 0; i < 10; i++) {
            global_variable_with_dynamic_rc.set_object(i, nullopt);
        }
    }
}