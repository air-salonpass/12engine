// Unit11: StructuredBufferを介し、512スレッドの計算結果を安全に集計する。
struct InputData { int init_value; int add_value; };
struct OutputData { int result_value; };
StructuredBuffer<InputData> data_input : register(t0);
RWStructuredBuffer<OutputData> data_output : register(u0);
static const int THREAD_X=8, THREAD_Y=8, THREAD_Z=8;
groupshared int temp_buffer[THREAD_X*THREAD_Y*THREAD_Z];

[numthreads(THREAD_X,THREAD_Y,THREAD_Z)]
void main(uint3 id : SV_DispatchThreadID, uint3 group_id : SV_GroupID, uint group_index : SV_GroupIndex)
{
    temp_buffer[group_index] = data_input[0].init_value + (group_index < 10 ? data_input[0].add_value : 0);
    if(group_index==0) data_output[0].result_value=0;
    GroupMemoryBarrierWithGroupSync();
    int total=0; [unroll] for(int i=0;i<THREAD_X*THREAD_Y*THREAD_Z;++i) total+=temp_buffer[i];
    InterlockedAdd(data_output[0].result_value,total);
}
