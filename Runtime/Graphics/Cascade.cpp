/*
Copyright(c) 2016-2017 Panos Karabelas

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

//= INCLUDES ========================
#include "Cascade.h"
#include "../Math/Matrix.h"
#include "../Logging/Log.h"
#include "../Components/Camera.h"
#include "../Core/Context.h"
#include "../Math/Vector4.h"
#include "D3D11/D3D11RenderTexture.h"
//===================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	Cascade::Cascade(int resolution, Camera* camera, Context* context)
	{
		auto graphics = context->GetSubsystem<Graphics>();
		m_depthMap = make_unique<D3D11RenderTexture>(graphics);
		m_depthMap->Create(resolution, resolution, true);
		m_camera = camera;
	}

	void Cascade::SetAsRenderTarget()
	{
		m_depthMap->Clear(0.0f, 0.0f, 0.0f, 1.0f);
		m_depthMap->SetAsRenderTarget();
	}

	ID3D11ShaderResourceView* Cascade::GetShaderResource()
	{
		return m_depthMap ? m_depthMap->GetShaderResourceView() : nullptr;
	}

	Matrix Cascade::ComputeProjectionMatrix(int cascadeIndex, const Vector3 centerPos, const Matrix& viewMatrix)
	{
		// Hardcoded size
		float extents = 0;
		if (cascadeIndex == 0)
			extents = 20;

		if (cascadeIndex == 1)
			extents = 40;

		if (cascadeIndex == 2)
			extents = 80;

		Vector3 center = centerPos * viewMatrix;
		Vector3 min = center - Vector3(extents, extents, extents);
		Vector3 max = center + Vector3(extents, extents, extents);

		return Matrix::CreateOrthoOffCenterLH(min.x, max.x, min.y, max.y, min.z, max.z);
	}

	float Cascade::GetSplit(int cascadeIndex)
	{
		if (!m_camera)
		{
			LOG_WARNING("Cascade split can't be computed, camera is not present.");
			return 0;
		}

		float splitDistance = 0;

		// Second cascade
		if (cascadeIndex == 1)
			splitDistance = 0.3f;

		// Third cascade
		if (cascadeIndex == 2)
			splitDistance = 0.6f;

		Vector4 shaderSplit = Vector4::Transform(Vector3(0, 0, splitDistance), m_camera->GetProjectionMatrix());
		return shaderSplit.z / shaderSplit.w;
	}
}
