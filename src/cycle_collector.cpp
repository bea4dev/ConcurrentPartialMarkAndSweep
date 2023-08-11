#include "cycle_collector.hpp"
#include "dynamic_rc.hpp"
#include <utility>


SpinLock list_lock{};
unordered_set<HeapObject*> suspected_object_set{};


/**
 * オブジェクトに着色する色
 */
enum object_color : uint8_t {
    red,
    gray,
    white,
    black
};


/**
 * ルートオブジェクトとそれに連なる全てのオブジェクトを、ロックを取得しながら赤に着色する
 */
void mark_red(HeapObject* root, HeapObject* current_object, unordered_map<HeapObject*, uint8_t>& color_map, vector<HeapObject*>& collect_objects, bool& is_cyclic_root);

/**
 * Mark gray phase
 * Partial mark and sweep と同様
 */
void mark_gray(HeapObject* current_object, unordered_map<HeapObject*, uint8_t>& color_map, unordered_map<HeapObject*, size_t>& count_map, bool is_first);

/**
 * Mark white phase
 * Partial mark and sweep と同様
 */
void mark_white(HeapObject* current_object, unordered_map<HeapObject*, uint8_t>& color_map, unordered_map<HeapObject*, size_t>& count_map);

/**
 * Mark black phase
 * Partial mark and sweep と同様 (ただしカウントの変更は行わない)
 */
void mark_black(HeapObject* current_object, unordered_map<HeapObject*, uint8_t>& color_map, unordered_map<HeapObject*, size_t>& count_map);

/**
 * 回収可能かどうかをチェック
 */
bool check_ready_to_collect(HeapObject* current_object, unordered_set<HeapObject*>& acyclic_objects);

SpinLock gc_lock{};


/**
 * >>> Concurrent Partial Mark and Sweep
 * 
 * - 参考文献
 *  + 『ガベージコレクション 自動的メモリ管理を構成する理論と実装』
 *  + https://pages.cs.wisc.edu/~cymen/misc/interests/Bacon01Concurrent.pdf
 * 
 * 基本的には単純に Partial Mark and Sweep に同時実行するための同期命令を加えたものである。
 * 参照の突然変異に対処するためにルートオブジェクトから辿ることのできる全てのオブジェクトに順次ロックをかけてから解放処理を行う。
 * また、非循環参照オブジェクトのデストラクタの呼び出しタイミングが決定的となるように、
 * 調査対象となるルートオブジェクトが循環参照の一部となるかどうかを調べ、場合分けを行いそれぞれ別々に解放する。
 * 具体的には、循環参照オブジェクトである場合は実行スレッドから解放できないためそのままこの gc で解放しても問題ないと見なすが、
 * 非循環参照オブジェクトの場合はデストラクタの呼び出しを実行スレッドに任せそのスレッド上で解放可能であることをマークし、
 * gc のスレッドが動作するまで開放を遅らせる(開放の責任を押し付ける)。
 */
void gc_collect() {
    //単一のスレッドでしか実行できないようにロック
    gc_lock.lock();

    //Swap suspected objects
    unordered_set<HeapObject*> roots;
    list_lock.lock();
    swap(roots, suspected_object_set);
    list_lock.unlock();

    //解放されるオブジェクトの集合
    unordered_set<HeapObject*> release_objects;

    for (auto& root : roots) {
        //オブジェクトの着色を一時的に記憶するためのマップ
        unordered_map<HeapObject*, uint8_t> color_map;
        //オブジェクトのカウントを一時的に記憶するためのマップ
        unordered_map<HeapObject*, size_t> count_map;
        //探索したオブジェクトのリスト
        vector<HeapObject*> collect_objects;

        //現在調べているルートオブジェクトが循環参照の輪の一部かどうか
        bool is_cyclic_root = false;

        //Mark red phase
        //ルートオブジェクトからロックを掛けつつ辿りながら、赤に着色する
        mark_red(root, root, color_map, collect_objects, is_cyclic_root);

        //現在調べているルートオブジェクトが循環参照の輪の一部かどうか
        if (is_cyclic_root) {
            //循環参照である場合
            
            //Mark gray phase
            //Partial mark and sweep と同様
            mark_gray(root, color_map, count_map, true);

            //Mark white or black phase
            //Partial mark and sweep と同様
            mark_white(root, color_map, count_map);
            
            //白にマークしたオブジェクトを開放可能なオブジェクトとしてマーク
            for (auto& object : collect_objects) {
                if (color_map[object] == object_color::white) {
                    object->ready_to_release_with_gc.store(true, memory_order_release);
                    release_objects.insert(object);
                }
            }

            //取得したロックを全て解除
            for (auto& object : collect_objects) {
                object->unlock();
            }
        } else {
            //非循環参照オブジェクトである場合

            //取得したロックを全て解除
            for (auto& object : collect_objects) {
                object->unlock();
            }

            //実行スレッド上で開放可能としてマークされているかどうかをチェック
            unordered_set<HeapObject*> acyclic_objects;
            bool ready_to_release = check_ready_to_collect(root, acyclic_objects);
            
            //マークされている場合は開放可能として記憶
            if (ready_to_release) {
                for (auto& object : acyclic_objects) {
                    release_objects.insert(object);
                }
            }
        }
    }

    //開放可能なオブジェクトに対する処理
    for (auto& object : release_objects) {
        //循環参照疑惑のあるルートの集合に含まれる場合は削除
        roots.erase(object);
        if (object->is_cyclic_type && object->buffered.load(std::memory_order_relaxed)) {
            list_lock.lock();
            suspected_object_set.erase(object);
            list_lock.unlock();
        }

        //開放する循環参照オブジェクトのフィールドオブジェクトのうち、
        //白にマークされなかったオブジェクトの参照カウントを一つ減らす
        auto** fields = (HeapObject**) (object + 1);
        size_t field_length = object->field_length;
        for (size_t i = 0; i < field_length; i++) {
            auto* field_object = fields[i];
            if (field_object != nullptr) {
                if (!field_object->ready_to_release_with_gc.load(memory_order_acquire)) {
                    DynamicRC rc(field_object);
                }
            }
        }
    }

    //開放可能なオブジェクトを開放
    for (auto& object : release_objects) {
        free(object);

        #if RC_VALIDATION
            //生存しているオブジェクト数を一つ減らす
            global_object_count.fetch_sub(1, memory_order_relaxed);
        #endif
    }

    //解放できなかったオブジェクトを再度回収を試みるために記憶しておく
    for (auto& root : roots) {
        list_lock.lock();
        suspected_object_set.insert(root);
        list_lock.unlock();
    }

    //gc 用のロックを解除
    gc_lock.unlock();
}




/**
 * ルートオブジェクトとそれに連なる全てのオブジェクトを、ロックを取得しながら赤に着色する
 */
void mark_red(HeapObject* root, HeapObject* current_object, unordered_map<HeapObject*, uint8_t>& color_map, vector<HeapObject*>& collect_objects, bool& is_cyclic_root) {
    //オブジェクトが既に着色されているかどうか
    if (color_map.find(current_object) != color_map.end()) {
        //されていれば処理を中断
        return;
    }

    //赤に着色
    color_map[current_object] = object_color::red;
    //ロックを取得
    current_object->lock();

    collect_objects.push_back(current_object);

    //オブジェクトの開始ポインタ
    auto** field_start_ptr = (HeapObject**) (current_object + 1);
    size_t field_length = current_object->field_length;

    for (size_t i = 0; i < field_length; i++) {
        auto* field_object = field_start_ptr[i];

        if (field_object != nullptr) {
            //フィールドのオブジェクトがルートオブジェクトと一致するかどうか
            if (field_object == root) {
                //一致していれば、ルートオブジェクトは循環参照の一部であることがわかる
                //その情報を is_cyclic_root へ反映する
                is_cyclic_root = true;
            }

            mark_red(root, field_object, color_map, collect_objects, is_cyclic_root);
        }
    }
}


/**
 * Mark gray phase
 * Partial mark and sweep と同様
 */
void mark_gray(HeapObject* current_object, unordered_map<HeapObject*, uint8_t>& color_map, unordered_map<HeapObject*, size_t>& count_map, bool is_first) {
    //オブジェクトが灰色に着色されているかどうか
    if (color_map[current_object] == object_color::gray) {
        //されていればカウントを一つ減らす
        count_map[current_object]--;
        //処理を中断
        return;
    } else {
        //されていなければ
        //灰色に着色
        color_map[current_object] = object_color::gray;

        //現在の参照カウントを取得
        auto ref_count = ((atomic_size_t*) &current_object->reference_count)->load(memory_order_acquire);
        //マークの開始点かどうか
        if (is_first) {
            //開始点であれば、単に現在の参照カウントを登録
            count_map[current_object] = ref_count;
        } else {
            //そうでなければ、参照カウントを一つ減らして登録
            count_map[current_object] = ref_count - 1;
        }
    }

    //オブジェクトの開始ポインタ
    auto** field_start_ptr = (HeapObject**) (current_object + 1);
    size_t field_length = current_object->field_length;

    //各フィールドのオブジェクトに対して mark gray を再帰的に呼び出す
    for (size_t i = 0; i < field_length; i++) {
        auto* field_object = field_start_ptr[i];

        if (field_object != nullptr) {
            mark_gray(field_object, color_map, count_map, false);
        }
    }
}


/**
 * Mark white phase
 * Partial mark and sweep と同様
 */
void mark_white(HeapObject* current_object, unordered_map<HeapObject*, uint8_t>& color_map, unordered_map<HeapObject*, size_t>& count_map) {
    //オブジェクトが灰色以外に着色されている場合は何もしない
    if (color_map[current_object] != object_color::gray) {
        return;
    }

    //参照カウントを取得
    auto ref_count = count_map[current_object];
    if (ref_count != 0) {
        //参照カウントが0でない場合は、処理を中断して mark black phase へ移行する
        mark_black(current_object, color_map, count_map);
        return;
    }

    //白に着色
    color_map[current_object] = object_color::white;

    //オブジェクトの開始ポインタ
    auto** field_start_ptr = (HeapObject**) (current_object + 1);
    size_t field_length = current_object->field_length;

    //各フィールドのオブジェクトに対して mark white を再帰的に呼び出す
    for (size_t i = 0; i < field_length; i++) {
        auto* field_object = field_start_ptr[i];

        if (field_object != nullptr) {
            mark_white(field_object, color_map, count_map);
        }
    }
}


/**
 * Mark black phase
 * Partial mark and sweep と同様 (ただしカウントの変更は行わない)
 */
void mark_black(HeapObject* current_object, unordered_map<HeapObject*, uint8_t>& color_map, unordered_map<HeapObject*, size_t>& count_map) {
    //オブジェクトが黒に着色されている場合は何もしない
    if (color_map[current_object] == object_color::black) {
        return;
    }

    //黒に着色
    color_map[current_object] = object_color::black;

    //オブジェクトの開始ポインタ
    auto** field_start_ptr = (HeapObject**) (current_object + 1);
    size_t field_length = current_object->field_length;

    //各フィールドのオブジェクトに対して mark black を再帰的に呼び出す
    for (size_t i = 0; i < field_length; i++) {
        auto* field_object = field_start_ptr[i];

        if (field_object != nullptr) {
            mark_black(field_object, color_map, count_map);
        }
    }
}


/**
 * 回収可能かどうかをチェック
 */
bool check_ready_to_collect(HeapObject* current_object, unordered_set<HeapObject*>& acyclic_objects) {
    //既にチェック済みである場合は無視
    if (acyclic_objects.find(current_object) != acyclic_objects.end()) {
        return true;
    }

    //オブジェクトが回収可能であるかどうかをチェック
    if (!current_object->ready_to_release_with_gc.load(memory_order_acquire)) {
        return false;
    }

    acyclic_objects.insert(current_object);

    current_object->lock();
    
    //フィールドのオブジェクトが回収可能であるかどうかを再帰的にチェック
    auto** fields = (HeapObject**) (current_object + 1);
    size_t field_length = current_object->field_length;
    for (size_t i = 0; i < field_length; i++) {
        auto* field_object = fields[i];
        if (field_object != nullptr) {
            auto result = check_ready_to_collect(field_object, acyclic_objects);
            if (!result) {
                current_object->unlock();
                return false;
            }
        }
    }

    current_object->unlock();

    return true;
}
