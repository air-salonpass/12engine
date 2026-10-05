#include "geometry_particle.hlsli"

GS_IN main(VS_IN vin, uint vid : SV_VertexID)
{
    GS_IN output = (GS_IN) 0;
    output.position = vin.position;
    output.color = vin.color;
    output.size = vin.size;
	
    output.param = vin.param;
    return output;
}
