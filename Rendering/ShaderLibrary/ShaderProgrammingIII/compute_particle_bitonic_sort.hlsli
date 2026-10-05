RWStructuredBuffer<particle_header> particle_header_buffer : register(u3);
// Unit13: 生存を優先し、同じ生存状態ならカメラから遠い粒子を先にする。
bool particle_before(particle_header a,particle_header b){ return a.alive>b.alive || (a.alive==b.alive && a.depth>b.depth); }
