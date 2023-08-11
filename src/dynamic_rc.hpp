#pragma once

#include "heap_object.hpp"
#include "cycle_collector.hpp"


/**
 * >> 元の動的変異即時参照カウント法に循環参照を回収するための機能を加えたもの
 * 
 * シングルスレッドモードとスレッドセーフモードを動的に切り替える即時参照カウント法
 * それぞれのモードについての個別な説明は single_thread_rc.hpp と thread_safe_rc.hpp を参照
 * それぞれのモードの基本的な考えはほぼ同一である。
 * 
 * ここではそれぞれのモード切り替えの妥当性と安全性について記す。
 * 
 * 通常、スレッドセーフな即時参照カウントはカウントの増減や複数のスレッドへ所有権を共有するタイミングで適切な同期処理を要する。
 * この同期処理は、ほとんどの場合それぞれ適切な atomic-read-modify-write や排他制御を必要とし[^1]、これが頻繁に発生する場合は
 * 無視できないほどのオーバーヘッドとなる(特にx86系プロセッサ)。
 * 特に単一スレッドでのみ使われるオブジェクトに関しては、これは明らかに無駄なオーバーヘッドとなる。
 * この問題を回避するために、実行時に極力 atomic なデータ型を使用せずに複数のスレッド間で共有されているかどうかを動的に判定し、
 * シングルスレッドモードとスレッドセーフモードを切り替える手法を提供する。
 * 
 * 具体的には、オブジェクのヘッダである HeapObject の is_mutex が true である場合、そのオブジェクトが複数のスレッドから
 * アクセスされうることを表し、これを用いてシングルスレッドモードとスレッドセーフモードを動的に切り替える。
 * これが通常の load/store と分岐命令で達成可能であることを以下に示す。
 * 
 * 
 * >>> 前提条件
 * まず、前提条件について言及する。
 * 
 *  1. Java等のようにオブジェクトは全てヒープ領域に確保して動的に管理されているものとし、
 *     ユーザーが扱うオブジェクトはすべて参照型とする
 * 
 *  2. 他のスレッドへオブジェクトを共有する場合はメモリを共有して行う(クロージャーでの変数共有を含む)
 * 
 *  3. グローバル変数(ローカル変数でない複数のスレッドからアクセス可能な変数)は、グローバル変数オブジェクトとして、
 *     予め複数のスレッドに共有されているオブジェクトとして考える
 *    > グローバル変数への挿入は変数オブジェクトのフィールドへの挿入として考える
 * 
 *  4. 基本的にアドレス計算による安全でないメモリ操作を許可しない処理系を前提とする
 * 
 * 
 * >>> 自明な事柄
 * 次に、前提条件をもとに自明な事柄について言及する
 * 
 *  1. オブジェクトが複数のスレッドから"直接"アクセスされうるコード内箇所はコンパイル時に全て検出可能である
 *    > これはグローバル変数オブジェクトとスレッドの立ち上げのタイミングを指す
 *    > グローバル変数はプロセス起動時から既に当てはまるものとして考えられる
 *    > スレッドの立ち上げのタイミングは、スレッドを起動するための関数に渡す引数がこれに当てはまる
 * 
 *  2. オブジェクトを複数のスレッドから"間接的"にアクセスするには全て1.のオブジェクト、
 *     若しくはそのオブジェクトのフィールドに連なるオブジェクトを経由する必要がある
 * 
 *  3. 複数のスレッドからアクセスされうる全てのオブジェクトは、1.と2.(直接か間接的にアクセスできるオブジェクト)のいずれかである
 * 
 * 
 * >>> アプローチの概要
 * 次に、アプローチの概要について説明する。
 * 
 *  1. まずコンパイル時に自明な事柄1.に当てはまる部分の初期化時に、オブジェクトの is_mutex を true に設定するコードを挿入する
 *     (予め複数のスレッドからアクセスされうることをマークしておく)
 * 
 *  2. オブジェクトA のフィールドへオブジェクトB を挿入する場合、
 *     A の is_mutex が true である場合は挿入前に B 以下のオブジェクト(オブジェクトB とそのフィールドに間接的に連なる全てのオブジェクト)の
 *     is_mutex も true に伝搬させる
 * 
 *  3. オブジェクトの is_mutex が true である場合、スレッドセーフな参照カウントとして振る舞い、
 *     そうでない場合はシングルスレッド専用の参照カウントとして振る舞う
 * 
 *  4. 一度 is_mutex を true に設定した後は、そのオブジェクトの寿命が尽きるまで変更しない
 * 
 * まず3.の安全性を示す。
 * 3.が表すのは is_mutex が true であれば、そのオブジェクトが複数のスレッドからアクセスされる可能性があるということである。
 * この情報は、自明な事柄1.2.とアプローチ1.2.4.により提供される。
 * 自明な事柄1.2.とアプローチ1.2.よりオブジェクトが複数のスレッドからアクセスされうる状態になる前に、is_mutex が true になることが分かる。
 * 直感的に説明すると、あるオブジェクトの is_mutex が true である状態はそのオブジェクトが複数のスレッドからアクセスされうることを表しており、
 * そのオブジェクトのフィールドに連なるすべてのオブジェクトは同じく複数のスレッドからアクセスされうる。
 * 加えて、それらのオブジェクトのフィールドに新しいオブジェクトの参照を持たせる場合はその新しいオブジェクトとそれに連なるすべてのオブジェクトの
 * is_mutex を true に設定してから操作を行えば、同じく is_mutex が複数のスレッドからアクセスされうるかどうかを少ない実行時コストで判定することができる。
 * ただし、この手法では実際に is_mutex が true の時、必ずしも複数のスレッドからアクセスされるとは限らない。
 * これは少なくとも前提条件の中で、複数のスレッドからアクセスする手段のないオブジェクトを検出してシングルスレッド専用の参照カウントを使用し、
 * 実行時のコストを削減するための手法である。
 * また、is_mutex が true であったとしても必ずしも複数のスレッドからアクセスされうるとは限らないケースが存在するが、
 * このパターンは安全であるので今回ここでは言及しない。
 * is_mutex への load/store については、単純に考えれば複数のスレッドからアクセスされるため順序関係に気を遣って実装する必要があるように思えるが、
 * この手法では二つの理由により is_mutex への load/store 自体に同期処理は必要ない。
 * 
 *  1. まず事柄1.のグローバル変数に関しては、他のスレッド起動前に is_mutex を true に変更し、それ以降書き込むことはないので(アプローチ4.)
 *     初期化スレッドをひとつに制限すればそれ以降でスレッドを立ち上げてもその時点で
 *     happens-before-relationship が明らかに成立するため通常の load と store 命令で十分である
 * 
 *  2. 次に事柄2.のオブジェクトは、is_mutex が true に変更される前と後で場合分けして考えるのが良い
 *     is_mutex が true に変更される前は、一つのスレッド内でしか操作できないため、この場合に限り同期処理は必要ない。
 *     is_mutex が true にされた後、つまり複数のスレッドからアクセスされうるオブジェクトのフィールドへの挿入処理には排他制御を必要とする[^1]。
 *     この排他制御のうち、オブジェクトを共有する側の unlock の release と読み取る側の lock の acquire により is_mutex の
 *     happens-before-relationship が成立する。
 *     加えてアプローチ4.より、is_mutex はそれ以降書き込まれることはない。
 * 
 * よって、is_mutex が true に変更される前と後、つまりオブジェクトが複数のスレッドからアクセス可能になる前と後で、
 * 自動的に順序関係が成立し is_mutex の値は通常の store と load 命令だけで正しい値を読むことができる。
 * このように複数のスレッドからアクセスされうるかどうかを動的に判定することで、安全かつ低コストに
 * シングルスレッドモードとスレッドセーフモードを動的に切り替え、必要の無い同期処理を削減する。
 * 特に、シングルスレッドモードでは一切の同期処理を必要としない。
 * 
 * 
 * [^1]: スレッドセーフな参照カウントは『ガベージコレクション 自動的メモリ管理を構成する理論と実装』の
 *       18章「並行参照カウント法」にて取り上げられているロックを用いた単純な並行即時参照カウント法を参考に実装している
 * 
 */
class DynamicRC {

private:
    //オブジェクト本体へのポインタ
    HeapObject* object_ref;

public:
    inline explicit DynamicRC(HeapObject* object_ref) {
        this->object_ref = object_ref;
    }

    /**
     * オブジェクトの is_mutex の値を変更して初期化
     */
    inline DynamicRC(HeapObject* object_ref, bool is_mutex) {
        object_ref->is_mutex = is_mutex;
        this->object_ref = object_ref;
    }

    /**
     * コピーコンストラクタ
     * コピー時に参照カウントを一つ増やす
     */
    inline DynamicRC(const DynamicRC& rc) {
        auto* object_ref = rc.object_ref;
        //このオブジェクトが複数のスレッドからアクセスされる可能性があるかどうか
        if (object_ref->is_mutex) {
            //可能性がある場合、atomic-read-modify-write により参照カウントを一つ増やす
            auto previous_ref_count = ((atomic_size_t*) &object_ref->reference_count)->fetch_add(1, memory_order_relaxed);

            //必要な場合に、オブジェクトを循環参照コレクタへ渡す
            try_add_suspected_object(object_ref, previous_ref_count);
        } else {
            //そうでない場合は、通常の命令で参照カウントを一つ増やす
            object_ref->reference_count++;
        }
        this->object_ref = object_ref;
    }

    /**
     * デストラクタ
     * 呼び出される度に参照カウントを一つ減らす
     */
    inline ~DynamicRC() {
        size_t previous_ref_count;

        //このオブジェクトが複数のスレッドからアクセスされる可能性があるかどうか
        if (this->object_ref->is_mutex) {
            //可能性がある場合、atomic-read-modify-write により参照カウントを一つ減らす
            previous_ref_count = ((atomic_size_t*) &this->object_ref->reference_count)->fetch_sub(1, memory_order_release);

            if (previous_ref_count == 1) {
                //減らした後の参照カウントが0である場合は他のスレッド上での変更を取得
                atomic_thread_fence(memory_order_acquire);

                //循環参照コレクタに監視されているかどうかをチェック
                if (this->object_ref->is_cyclic_type && this->object_ref->buffered.load(memory_order_relaxed)) {
                    //そうである場合は専用の関数で代わりに解放処理を行う
                    drop_object_for_cyclic_type(this->object_ref);
                    return;
                }
            }
        } else {
            //そうでない場合は、通常の命令で参照カウントを一つ減らす
            previous_ref_count = this->object_ref->reference_count--;
        }

        if (previous_ref_count != 1) {
            //減らした後の参照カウントが0でない場合は何もしない
            return;
        }

        auto field_length = this->object_ref->field_length;
        //フィールドの開始ポインタ
        auto** field_start_ptr = (HeapObject**) (this->object_ref + 1);

        //フィールドに格納されている全オブジェクトの参照カウントを一つ減らす
        for (size_t field_index = 0; field_index < field_length; field_index++) {
            //対象となるフィールドのポインタ
            auto** field_ptr = field_start_ptr + field_index;
            //フィールドの内容をロード
            auto* field_object = *field_ptr;

            if (field_object != nullptr) {
                //デストラクタを呼び出し、参照カウントを一つ減らす
                DynamicRC rc(field_object);
            }
        }

        free(this->object_ref);


        #if RC_VALIDATION
            //生存しているオブジェクト数を一つ減らす
            global_object_count.fetch_sub(1, memory_order_relaxed);
        #endif
    }


    /**
     * オブジェクトの spin_lock_flag を使用してスピンロック(lock)
     */
    inline void lock() {
        while (this->object_ref->spin_lock_flag.test_and_set(memory_order_acquire)) {
            //spin
        }
    }

    /**
     * オブジェクトの spin_lock_flag を使用してスピンロック(unlock)
     */
    inline void unlock() {
        this->object_ref->spin_lock_flag.clear(memory_order_release);
    }



    /**
     * 指定された番号のフィールドにオブジェクト若くは nullptr を挿入
     */
    inline void set_object(size_t field_index, optional<DynamicRC> rc) {
        //rc が nullopt であれば nullptr
        //そうでなければオブジェクトへのポインタを取得
        HeapObject* object = nullptr;
        if (rc.has_value()) {
            object = rc.value().object_ref;
        }

        //フィールドの開始ポインタ
        auto** field_start_ptr = (HeapObject**) (this->object_ref + 1);
        //対象となるフィールドのポインタ
        auto** field_ptr = field_start_ptr + field_index;

        if (object != nullptr) {
            //挿入するオブジェクトが複数のスレッドからアクセスされる可能性があるかどうか
            if (object->is_mutex) {
                //可能性がある場合、atomic-read-modify-write により参照カウントを一つ増やす
                auto previous_ref_count = ((atomic_size_t*) &object->reference_count)->fetch_add(1, memory_order_relaxed);

                //必要な場合に、オブジェクトを循環参照コレクタへ渡す
                try_add_suspected_object(object, previous_ref_count);
            } else {
                //そうでない場合は、通常の命令で参照カウントを一つ増やす
                object->reference_count++;
            }
        }
        

        HeapObject* field_old_object;

        //このオブジェクトが複数のスレッドからアクセスされる可能性があるかどうか
        if (this->object_ref->is_mutex) {
            //可能性がある場合

            if (object != nullptr) {
                //挿入対象のオブジェクト以下のオブジェクト(フィールドに間接的に連なる全てのオブジェクトを含む)の is_mutex を true に伝搬させる
                object->to_mutex();
            }
            
            //スピンロックを使って安全に入れ替える
            //この lock によりL266の to_mutex() の結果を acquire できる 
            this->lock();
            field_old_object = *field_ptr;
            *field_ptr = object;
            //この unlock によりL266の to_mutex() の結果が release される 
            this->unlock();
        } else {
            //そうでない場合
            //通常の命令で入れ替える
            field_old_object = *field_ptr;
            *field_ptr = object;
        }

        if (field_old_object != nullptr) {
            //デストラクタを呼び出し、既に挿入されていたオブジェクトの参照カウントを一つ減らす
            DynamicRC rc(field_old_object);
        }
    }


    /**
     * 指定された番号のフィールドにあるオブジェクトを取得
     */
    inline optional<DynamicRC> get_object(size_t field_index) {
        //フィールドの開始ポインタ
        auto** field_start_ptr = (HeapObject**) (this->object_ref + 1);
        //対象となるフィールドのポインタ
        auto** field_ptr = field_start_ptr + field_index;


        HeapObject* field_object;

        //このオブジェクトが複数のスレッドからアクセスされる可能性があるかどうか
        if (this->object_ref->is_mutex) {
            //可能性がある場合
            //スピンロックを使用して以下の操作を不可分的に行う
            // 1. フィールドからロード
            // 2. ロードしたオブジェクトの参照カウントを一つ増やす
            //オブジェクトの削除処理との順序関係を確定させるために不可分操作を要する
            //この lock によりL266の to_mutex() の結果を acquire できる 
            this->lock();
            field_object = *field_ptr;
            if (field_object != nullptr) {
                //this->object_ref の is_mutex が true であり、
                //アプローチ2.より field_object の is_mutex が true であることがわかるためチェックする必要はない
                auto previous_ref_count = ((atomic_size_t*) &field_object->reference_count)->fetch_add(1, memory_order_relaxed);

                //必要な場合に、オブジェクトを循環参照コレクタへ渡す
                try_add_suspected_object(field_object, previous_ref_count);
            }
            this->unlock();
        } else {
            //そうでない場合
            //通常の命令で取得する
            field_object = *field_ptr;
            if (field_object != nullptr) {
                //取得したオブジェクトが複数のスレッドからアクセスされる可能性があるかどうか
                if (field_object->is_mutex) {
                    //可能性がある場合、atomic-read-modify-write により参照カウントを一つ増やす
                    auto previous_ref_count = ((atomic_size_t*) &field_object->reference_count)->fetch_add(1, memory_order_relaxed);

                    //必要な場合に、オブジェクトを循環参照コレクタへ渡す
                    try_add_suspected_object(field_object, previous_ref_count);
                } else {
                    //そうでない場合は、通常の命令で参照カウントを一つ増やす
                    field_object->reference_count++;
                }
            }
        }

        if (field_object == nullptr) {
            return nullopt;
        } else {
            return DynamicRC(field_object);
        }
    }

    inline void to_mutex() {
        this->object_ref->to_mutex();
    }

    inline void mark_as_cyclic_type() {
        this->object_ref->is_cyclic_type = true;
        this->object_ref->is_mutex = true;
    }

    inline size_t get_reference_count() {
        if (this->object_ref->is_mutex) {
            return ((atomic_size_t*) &this->object_ref->reference_count)->load(memory_order_relaxed);
        } else {
            return this->object_ref->reference_count;
        }
    }

};
