/*
Copyright(c) 2016 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

//= INCLUDES ====================
#include "../Graphics.h"
#include "../D3D11/D3D11Buffer.h"
#include "../D3D11/D3D11Shader.h"
#include "../../Math/Matrix.h"
//===============================

class PostProcessShader
{
public:
	PostProcessShader();
	~PostProcessShader();

	void Initialize(LPCSTR pass, Graphics* d3d11device);
	void Render(int indexCount, const Directus::Math::Matrix& worldMatrix, const Directus::Math::Matrix& viewMatrix, const Directus::Math::Matrix& projectionMatrix, ID3D11ShaderResourceView* texture);

private:
	struct DefaultBuffer
	{
		Directus::Math::Matrix worldViewProjection;
		Directus::Math::Vector2 viewport;
		Directus::Math::Vector2 padding;
	};
	D3D11Buffer* m_constantBuffer;

	Graphics* m_graphics;
	D3D11Shader* m_shader;
};
