struct VSOut {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

VSOut main(uint id : SV_VertexID) {
    float2 pos = float2((id << 1) & 2, id & 2);

    VSOut o;
    o.pos = float4(pos * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    o.uv = pos;
    return o;
}
