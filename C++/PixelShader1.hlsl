

Texture2D tx : register(t0);
SamplerState samLinear : register(s0);

struct PS_INPUT
{
	float4 Pos : SV_POSITION;
	float2 Tex : TEXCOORD;
};

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS(PS_INPUT input) : SV_Target
{
	float2 ndc = input.Tex * 2 - 1;

	float rsq = ndc.x*ndc.x + ndc.y*ndc.y;

	float scale = 1 + 0.22*rsq + 0.24*rsq*rsq;

	float2 distortedndc = ndc * scale + 0.1;

	float2 distorteduv = (distortedndc + 1)*0.5;

	if (distorteduv.x > 1.0 || distorteduv.x < 0.0 || distorteduv.y > 1.0 || distorteduv.y < 0.0)
	{
		return float4(0, 0, 0, 0);
	}
	else
	{
		return tx.Sample(samLinear, distorteduv);
	}
}