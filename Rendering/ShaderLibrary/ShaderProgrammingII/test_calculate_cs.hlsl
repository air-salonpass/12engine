
//  単純な計算をしてみる
#define	UNIT_15_SUB_01

//  スレッドと共有メモリを使ってみる
#define	UNIT_15_SUB_02

//  スレッドの動機待ちを行う
#define	UNIT_15_SUB_03



//  入力用構造体
struct InputData
{
    int init_value;
    int add_value;
};

//  出力用構造体
struct OutputData
{
    int result_value;
};

//  計算開始が入っているバッファ
StructuredBuffer<InputData> data_input;

//  計算結果を書き込むバッファ
RWStructuredBuffer<OutputData> data_output;

#if defined(UNIT_15_SUB_02)
static const int THREAD_X = 8;
static const int THREAD_Y = 8;
static const int THREAD_Z = 8;
static const int THREAD_COUNT = THREAD_X * THREAD_Y * THREAD_Z;

//  1つのスレッドグループ内のスレッド間で共有できるメモリ
//  スレッドグループ間で情報を受け渡したりする事が出来たり、
//  テクスチャの情報をあらかじめ読み込んでキャッシュできます(<= ココ重要)
//  ※groupshared はサイズが合わせて 32KiB までとなっている。
groupshared int temp_buffer[THREAD_COUNT];

//  実行するスレッド数の設定
//  (x, y, z) で一つのスレッドグループとして実行される。
//  実行するスレッド数の計算は x * y * z = スレッド数 となる。
//  これの総数が768/1024を超えることはできない。
//  ・SM4.0/4.1 = 768
//  ・SM5.0/5.1 = 1024 <= これに変更
[numthreads(THREAD_X, THREAD_Y, THREAD_Z)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    //  DTidはスレッドの実行中の番号を取得することができるので下記の様にして配列番号を算出
    int array_index = (DTid.x % THREAD_X)
                    + ((DTid.y % THREAD_Y) * THREAD_X)
                    + ((DTid.z % THREAD_Z) * THREAD_Y * THREAD_X);

    //  バッファから情報を取得して共有メモリに初期値を納入
    temp_buffer[array_index] = data_input[0].init_value;

    //  最初の0番のスレッドで計算結果バッファをクリア
    if (DTid.x + DTid.y + DTid.z == 0)
    {
        data_output[0].result_value = 0;
    }

    //  10番目ののスレッドだけ初期値に加算しておく
    if (array_index < 10)
    {
        temp_buffer[array_index] += data_input[0].add_value;
    }

#if defined(UNIT_15_SUB_03)
    //  同期用関数
    //  同じグループ内の他スレッドの処理がここに到達するまで待機
    GroupMemoryBarrierWithGroupSync();
#else   //  defined(UNIT_15_SUB_03)
    //  結果が不定になる問題を見せる。
#endif  //  defined(UNIT_15_SUB_03)

    //  共有メモリに設定した情報を単純に足していく
    int value = 0;
    for (int index = 0; index < THREAD_COUNT; ++index)
    {
        value += temp_buffer[index];
    }

    //  計算結果を同期して加算
    InterlockedAdd(data_output[0].result_value, value);
}
#elif defined(UNIT_15_SUB_01)
//  シンプルな計算処理をやってもらう
[numthreads(1, 1, 1)]
void main()
{
    //  計算用の情報を取得
    int value = data_input[0].init_value;
    int add_value = data_input[0].add_value;
    
    //  計算
    value += add_value;

    //  出力
    data_output[0].result_value = value;
}
#else // defined(UNIT_15_SUB_01)
[numthreads(1, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
//  特に何もしない
}
#endif // defined(UNIT_15_SUB_01)
