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

//= INCLUDES =================
#include <vector>
#include "../Graphics.h"
//============================

class D3D11InputLayout
{
public:
	D3D11InputLayout();
	~D3D11InputLayout();

	//= MISC ====================================
	void Initialize(Graphics* d3d11Device);
	void Set();
	InputLayout GetInputLayout();

	//= LAYOUT CREATION ============================================================================
	bool Create(ID3D10Blob* VSBlob, D3D11_INPUT_ELEMENT_DESC* vertexInputLayout, UINT elementCount);
	bool Create(ID3D10Blob* VSBlob, InputLayout layout);

private:
	//= LAYOUTS ===============================
	bool CreatePosDesc(ID3D10Blob* VSBlob);
	bool CreatePosColDesc(ID3D10Blob* VSBlob);
	bool CreatePosTexDesc(ID3D10Blob* VSBlob);
	bool CreatePosTexNorTanDesc(ID3D10Blob* VSBlob);

	Graphics* m_graphics;
	ID3D11InputLayout* m_ID3D11InputLayout;
	InputLayout m_inputLayout;
	std::vector<D3D11_INPUT_ELEMENT_DESC> m_layoutDesc;
};
