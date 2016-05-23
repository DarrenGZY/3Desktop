Texture2D tx_right : register(t0);
Texture2D tx_left : register(t1);
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

	float2 distortedndc = ndc * scale;

	float2 distorteduv = (distortedndc + 1)*0.5;

	if (distorteduv.x > 1.0 || distorteduv.x < 0.0 || distorteduv.y > 1.0 || distorteduv.y < 0.0)
	{
		return float4(0, 0, 0, 0);
	}
	else
	{
		if (input.Pos.x <= 1280) // half of the screen width
		{
			return tx_left.Sample(samLinear, distorteduv);
		}
		else 
		{
			return tx_right.Sample(samLinear, distorteduv);
		}
	}
}