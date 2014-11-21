// Copyright (c) Microsoft Corporation. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may
// not use these files except in compliance with the License. You may obtain
// a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations
// under the License.

#include "pch.h"
#include "CanvasSwapChain.h"
#include "MockDXGIAdapter.h"
#include "MockDXGISwapChain.h"
#include "MockDXGIFactory.h"
#include "TestDeviceResourceCreationAdapter.h"

TEST_CLASS(CanvasSwapChainUnitTests)
{
    class MockCanvasSwapChainDrawingSessionFactory : public ICanvasSwapChainDrawingSessionFactory
    {
    public:
        CALL_COUNTER_WITH_MOCK(CreateMethod, ComPtr<ICanvasDrawingSession>(ICanvasDevice*, IDXGISwapChain2*, Color const&));
        virtual ComPtr<ICanvasDrawingSession> Create(
            ICanvasDevice* owner,
            IDXGISwapChain2* swapChainResource,
            Color const& clearColor) const override
        {
            return CreateMethod.WasCalled(owner, swapChainResource, clearColor);
        }
    };

    struct StubDeviceFixture
    {
        std::shared_ptr<CanvasSwapChainManager> m_swapChainManager;
        std::shared_ptr<MockCanvasSwapChainDrawingSessionFactory> m_drawingSessionFactory;
        ComPtr<StubCanvasDevice> m_canvasDevice;

        StubDeviceFixture()
        {
            m_canvasDevice = Make<StubCanvasDevice>();
            m_swapChainManager = std::make_shared<CanvasSwapChainManager>();
            m_drawingSessionFactory = std::make_shared<MockCanvasSwapChainDrawingSessionFactory>();
            
            m_canvasDevice->CreateSwapChainMethod.AllowAnyCall([=](int32_t, int32_t, DirectXPixelFormat, int32_t, CanvasAlphaBehavior)
            {
                return Make<MockDxgiSwapChain>();
            });
        }

        ComPtr<CanvasSwapChain> CreateTestSwapChain()
        {
            return m_swapChainManager->Create(
                m_canvasDevice.Get(),
                1,
                1,
                DirectXPixelFormat::B8G8R8A8UIntNormalized,
                2,
                CanvasAlphaBehavior::Premultiplied,
                m_drawingSessionFactory);
        }
    };

    TEST_METHOD_EX(CanvasSwapChain_Creation)
    {
        StubDeviceFixture f;

        f.m_canvasDevice->CreateSwapChainMethod.SetExpectedCalls(1, 
            [=](int32_t widthInPixels, int32_t heightInPixels, DirectXPixelFormat format, int32_t bufferCount, CanvasAlphaBehavior alphaBehavior)
            {
                Assert::AreEqual(23, widthInPixels);
                Assert::AreEqual(45, heightInPixels);
                Assert::AreEqual(DirectXPixelFormat::B8G8R8A8UIntNormalizedSrgb, format);
                Assert::AreEqual(4, bufferCount);
                Assert::AreEqual(CanvasAlphaBehavior::Ignore, alphaBehavior);
                return Make<MockDxgiSwapChain>();
            });

        auto swapChain = f.m_swapChainManager->Create(
            f.m_canvasDevice.Get(),
            23,
            45,
            DirectXPixelFormat::B8G8R8A8UIntNormalizedSrgb,
            4,
            CanvasAlphaBehavior::Ignore,
            f.m_drawingSessionFactory);
    }

    struct FullDeviceFixture
    {
    public:
        ComPtr<CanvasDevice> m_canvasDevice;
        std::shared_ptr<CanvasSwapChainManager> m_swapChainManager;
        std::shared_ptr<MockCanvasSwapChainDrawingSessionFactory> m_drawingSessionFactory;

        FullDeviceFixture()
        {
            // Validating certain parameters requires an actual CanvasDevice, not a 
            // mock object.

            auto mockDxgiDevice = Make<MockDxgiDevice>();
            mockDxgiDevice->MockGetParent = 
                [&](IID const& iid, void** out)
                {
                    Assert::AreEqual(__uuidof(IDXGIAdapter2), iid);
                    auto mockAdapter = Make<MockDxgiAdapter>();

                    mockAdapter->GetParentMethod.SetExpectedCalls(1, 
                    [&](IID const& iid, void** out)
                    {
                        Assert::AreEqual(__uuidof(IDXGIFactory2), iid);
                        auto mockFactory = Make<MockDxgiFactory>();
                        mockFactory.CopyTo(reinterpret_cast<IDXGIFactory2**>(out));

                        return S_OK;

                    });

                    mockAdapter.CopyTo(reinterpret_cast<IDXGIAdapter2**>(out));

                    return S_OK;
                };

            auto d2dDevice = Make<MockD2DDevice>(mockDxgiDevice.Get());

            auto resourceCreationAdapter = std::make_shared<TestDeviceResourceCreationAdapter>();
            auto deviceManager = std::make_shared<CanvasDeviceManager>(resourceCreationAdapter);

            m_canvasDevice = deviceManager->GetOrCreate(d2dDevice.Get());

            m_swapChainManager = std::make_shared<CanvasSwapChainManager>();

            m_drawingSessionFactory = std::make_shared<MockCanvasSwapChainDrawingSessionFactory>();
        }

        ComPtr<CanvasSwapChain> CreateTestSwapChain(int32_t width, int32_t height, int32_t bufferCount)
        {
            return m_swapChainManager->Create(
                m_canvasDevice.Get(),
                width,
                height,
                DirectXPixelFormat::B8G8R8A8UIntNormalized,
                bufferCount,
                CanvasAlphaBehavior::Premultiplied,
                m_drawingSessionFactory);
        }
    };

    TEST_METHOD_EX(CanvasSwapChain_Creation_InvalidParameters)
    {
        FullDeviceFixture f0;
        ExpectHResultException(E_INVALIDARG, [&]  {  f0.CreateTestSwapChain(-1, 100, 2); });

        FullDeviceFixture f1;
        ExpectHResultException(E_INVALIDARG, [&]  {  f1.CreateTestSwapChain(123, -3, 2); });

        FullDeviceFixture f2;
        ExpectHResultException(E_INVALIDARG, [&]  {  f2.CreateTestSwapChain(100, 100, -3); });
    }

    TEST_METHOD_EX(CanvasSwapChain_Closed)
    {
        StubDeviceFixture f;

        auto canvasSwapChain = f.CreateTestSwapChain();

        Assert::AreEqual(S_OK, canvasSwapChain->Close());

        ComPtr<ICanvasDrawingSession> drawingSession;
        Assert::AreEqual(RO_E_CLOSED, canvasSwapChain->CreateDrawingSession(Color{}, &drawingSession));

        int32_t i;
        Assert::AreEqual(RO_E_CLOSED, canvasSwapChain->get_Width(&i));
        Assert::AreEqual(RO_E_CLOSED, canvasSwapChain->get_Height(&i));

        DirectXPixelFormat pixelFormat;
        Assert::AreEqual(RO_E_CLOSED, canvasSwapChain->get_Format(&pixelFormat));

        Assert::AreEqual(RO_E_CLOSED, canvasSwapChain->get_BufferCount(&i));

        CanvasAlphaBehavior alphaBehavior;
        Assert::AreEqual(RO_E_CLOSED, canvasSwapChain->get_AlphaMode(&alphaBehavior));

        Assert::AreEqual(RO_E_CLOSED, canvasSwapChain->ResizeBuffers(2, 2, 2, DirectXPixelFormat::B8G8R8A8UIntNormalized));

        ComPtr<ICanvasDevice> device;
        Assert::AreEqual(RO_E_CLOSED, canvasSwapChain->get_Device(&device));
    }


    TEST_METHOD_EX(CanvasSwapChain_NullArgs)
    {
        StubDeviceFixture f;

        auto canvasSwapChain = f.CreateTestSwapChain();

        Assert::AreEqual(E_INVALIDARG, canvasSwapChain->CreateDrawingSession(Color{}, nullptr));
        Assert::AreEqual(E_INVALIDARG, canvasSwapChain->get_Width(nullptr));
        Assert::AreEqual(E_INVALIDARG, canvasSwapChain->get_Height(nullptr));
        Assert::AreEqual(E_INVALIDARG, canvasSwapChain->get_Format(nullptr));
        Assert::AreEqual(E_INVALIDARG, canvasSwapChain->get_BufferCount(nullptr));
        Assert::AreEqual(E_INVALIDARG, canvasSwapChain->get_AlphaMode(nullptr));
        Assert::AreEqual(E_INVALIDARG, canvasSwapChain->get_Device(nullptr));
    }

    void ResetForPropertyTest(ComPtr<MockDxgiSwapChain>& swapChain)
    {
        swapChain->GetDesc1Method.SetExpectedCalls(1,
            [=](DXGI_SWAP_CHAIN_DESC1* desc)
            {
                desc->Width = 123;
                desc->Height = 456;
                desc->Format = DXGI_FORMAT_R16G16B16A16_UNORM;
                desc->BufferCount = 5;
                desc->AlphaMode = DXGI_ALPHA_MODE_IGNORE;
                return S_OK;
            });
    }

    TEST_METHOD_EX(CanvasSwapChain_Properties)
    {
        StubDeviceFixture f;

        auto swapChain = Make<MockDxgiSwapChain>();
            
        f.m_canvasDevice->CreateSwapChainMethod.AllowAnyCall([=](int32_t, int32_t, DirectXPixelFormat, int32_t, CanvasAlphaBehavior)
        {
            return swapChain;
        });

        auto canvasSwapChain = f.CreateTestSwapChain();

        int32_t i;
        DirectXPixelFormat pixelFormat;
        CanvasAlphaBehavior alphaBehavior;
        ComPtr<ICanvasDevice> device;

        ResetForPropertyTest(swapChain);
        ThrowIfFailed(canvasSwapChain->get_Width(&i));
        Assert::AreEqual(123, i);

        ResetForPropertyTest(swapChain);
        ThrowIfFailed(canvasSwapChain->get_Height(&i));
        Assert::AreEqual(456, i);

        ResetForPropertyTest(swapChain);
        ThrowIfFailed(canvasSwapChain->get_Format(&pixelFormat));
        Assert::AreEqual(DirectXPixelFormat::R16G16B16A16UIntNormalized, pixelFormat);

        ResetForPropertyTest(swapChain);
        ThrowIfFailed(canvasSwapChain->get_BufferCount(&i));
        Assert::AreEqual(5, i);

        ResetForPropertyTest(swapChain);
        ThrowIfFailed(canvasSwapChain->get_AlphaMode(&alphaBehavior));
        Assert::AreEqual(CanvasAlphaBehavior::Ignore, alphaBehavior);

        ThrowIfFailed(canvasSwapChain->get_Device(&device));
        Assert::AreEqual(device.Get(), static_cast<ICanvasDevice*>(f.m_canvasDevice.Get()));
    }

    TEST_METHOD_EX(CanvasSwapChain_ResizeBuffers)
    {
        StubDeviceFixture f;

        f.m_canvasDevice->CreateSwapChainMethod.AllowAnyCall([=](int32_t, int32_t, DirectXPixelFormat, int32_t, CanvasAlphaBehavior)
        {
            auto swapChain = Make<MockDxgiSwapChain>();

            swapChain->ResizeBuffersMethod.SetExpectedCalls(1,
                [=](
                UINT bufferCount,
                UINT width,
                UINT height,
                DXGI_FORMAT newFormat,
                UINT swapChainFlags)
            {
                Assert::AreEqual(3u, bufferCount);
                Assert::AreEqual(555u, width);
                Assert::AreEqual(666u, height);
                Assert::AreEqual(DXGI_FORMAT_R8G8B8A8_UNORM, newFormat);
                Assert::AreEqual(0u, swapChainFlags);
                return S_OK;
            });

            return swapChain;
        });

        auto canvasSwapChain = f.CreateTestSwapChain();

        ThrowIfFailed(canvasSwapChain->ResizeBuffers(3, 555, 666, DirectXPixelFormat::R8G8B8A8UIntNormalized));
    }

    TEST_METHOD_EX(CanvasSwapChain_Present)
    {
        StubDeviceFixture f;

        f.m_canvasDevice->CreateSwapChainMethod.AllowAnyCall([=](int32_t, int32_t, DirectXPixelFormat, int32_t, CanvasAlphaBehavior)
        {
            auto swapChain = Make<MockDxgiSwapChain>();

            swapChain->Present1Method.SetExpectedCalls(1,
                [=](
                UINT syncInterval,
                UINT presentFlags,
                const DXGI_PRESENT_PARAMETERS* presentParameters)
                {
                    Assert::AreEqual(1u, syncInterval);
                    Assert::AreEqual(0u, presentFlags);
                    Assert::IsNotNull(presentParameters);
                    Assert::AreEqual(0u, presentParameters->DirtyRectsCount);
                    Assert::IsNull(presentParameters->pDirtyRects);
                    Assert::IsNull(presentParameters->pScrollOffset);
                    Assert::IsNull(presentParameters->pScrollRect);
                    return S_OK;
                });

            return swapChain;
        });

        auto canvasSwapChain = f.CreateTestSwapChain();

        ThrowIfFailed(canvasSwapChain->Present());
    }

    TEST_METHOD_EX(CanvasSwapChain_CreateDrawingSession)
    {
        StubDeviceFixture f;

        Color expectedClearColor = { 1, 2, 3, 4 };

        f.m_drawingSessionFactory->CreateMethod.SetExpectedCalls(1, 
            [&] (ICanvasDevice* owner, IDXGISwapChain2* swapChainResource, Color const& clearColor)
            {
                Assert::AreEqual(expectedClearColor, clearColor);
                Assert::AreEqual(static_cast<ICanvasDevice*>(f.m_canvasDevice.Get()), owner);
                Assert::IsNotNull(swapChainResource);

                return Make<MockCanvasDrawingSession>();
            });

        auto canvasSwapChain = f.CreateTestSwapChain();

        ComPtr<ICanvasDrawingSession> drawingSession;
        ThrowIfFailed(canvasSwapChain->CreateDrawingSession(expectedClearColor, &drawingSession));
    }

    struct DrawingSessionAdapterFixture
    {
        std::shared_ptr<CanvasSwapChainManager> m_swapChainManager;
        ComPtr<StubCanvasDevice> m_canvasDevice;
        std::shared_ptr<CanvasSwapChainDrawingSessionFactory> m_drawingSessionFactory;
        ComPtr<MockD2DDeviceContext> m_deviceContext;

        enum Options
        {
            None,
            FailDuringClear
        };

        DrawingSessionAdapterFixture(Options options = None)
        {
            m_swapChainManager = std::make_shared<CanvasSwapChainManager>();

            m_drawingSessionFactory = std::make_shared<CanvasSwapChainDrawingSessionFactory>();

            m_deviceContext = Make<MockD2DDeviceContext>();

            auto expectedTargetBitmap = Make<StubD2DBitmap>();
            DXGI_FORMAT expectedFormat = DXGI_FORMAT_R16G16B16A16_UNORM;

            auto expectedBackBufferSurface = Make<MockDxgiSurface>();

            auto d2dDevice = Make<MockD2DDevice>();
            d2dDevice->MockCreateDeviceContext =
                [this, expectedBackBufferSurface, expectedFormat, expectedTargetBitmap, options](D2D1_DEVICE_CONTEXT_OPTIONS deviceContextOptions, ID2D1DeviceContext1** value)
                {
                    Assert::AreEqual(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, deviceContextOptions);

                    m_deviceContext->CreateBitmapFromDxgiSurfaceMethod.SetExpectedCalls(1,
                        [expectedBackBufferSurface, expectedFormat, expectedTargetBitmap](IDXGISurface* surface, const D2D1_BITMAP_PROPERTIES1* properties, ID2D1Bitmap1** out)
                        {
                            Assert::AreEqual(static_cast<IDXGISurface*>(expectedBackBufferSurface.Get()), surface);
                            Assert::IsNotNull(properties);
                            Assert::IsNull(properties->colorContext);
                            Assert::AreEqual(DEFAULT_DPI, properties->dpiX);
                            Assert::AreEqual(DEFAULT_DPI, properties->dpiY);
                            Assert::AreEqual(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, properties->bitmapOptions);
                            Assert::AreEqual(expectedFormat, properties->pixelFormat.format);
                            Assert::AreEqual(D2D1_ALPHA_MODE_IGNORE, properties->pixelFormat.alphaMode);

                            ThrowIfFailed(expectedTargetBitmap.CopyTo(out));

                            return S_OK;
                        });

                    m_deviceContext->SetTargetMethod.SetExpectedCalls(1,
                        [expectedTargetBitmap](ID2D1Image* target)
                        {
                            Assert::AreEqual(static_cast<ID2D1Image*>(expectedTargetBitmap.Get()), target);
                        });

                    m_deviceContext->BeginDrawMethod.SetExpectedCalls(1,
                        []()
                        {

                        });
                    
                    m_deviceContext->ClearMethod.SetExpectedCalls(1,
                        [options](D2D1_COLOR_F const* color)
                        {
                            if (options == FailDuringClear)
                            {
                                ThrowHR(E_NOTIMPL);
                            }

                            D2D1_COLOR_F d2dBlue = D2D1::ColorF(0, 0, 1);
                            Assert::AreEqual(d2dBlue, *color);
                        });

                    m_deviceContext->EndDrawMethod.SetExpectedCalls(1,
                        [](D2D1_TAG*, D2D1_TAG*)
                        {
                            return S_OK;
                        });

                    ThrowIfFailed(m_deviceContext.CopyTo(value));
                };

            m_canvasDevice = Make<StubCanvasDevice>(d2dDevice);
            
            m_canvasDevice->CreateSwapChainMethod.AllowAnyCall([=](int32_t, int32_t, DirectXPixelFormat, int32_t, CanvasAlphaBehavior)
            {
                auto swapChain = Make<MockDxgiSwapChain>();

                swapChain->GetDesc1Method.SetExpectedCalls(1,
                    [=](DXGI_SWAP_CHAIN_DESC1* desc)
                    {
                        // The only fields that should get used are the format and alpha mode.
                        DXGI_SWAP_CHAIN_DESC1 zeroed = {};
                        *desc = zeroed;

                        desc->Format = expectedFormat;
                        desc->AlphaMode = DXGI_ALPHA_MODE_IGNORE;
                        return S_OK;
                    });

                swapChain->GetBufferMethod.SetExpectedCalls(1,
                    [=](UINT index, const IID& iid, void** out)
                    {
                        Assert::AreEqual(0u, index);
                        Assert::AreEqual(__uuidof(IDXGISurface2), iid);

                        ThrowIfFailed(expectedBackBufferSurface.CopyTo(reinterpret_cast<IDXGISurface2**>(out)));

                        return S_OK;
                    });

                return swapChain;
            });
        }

        ComPtr<CanvasSwapChain> CreateTestSwapChain()
        {
            return m_swapChainManager->Create(
                m_canvasDevice.Get(),
                1,
                1,
                DirectXPixelFormat::B8G8R8A8UIntNormalized,
                2,
                CanvasAlphaBehavior::Premultiplied,
                m_drawingSessionFactory);
        }
    };

    TEST_METHOD_EX(CanvasSwapChain_DrawingSessionAdapter)
    {
        //
        // This test verifies the interaction between drawing session creation and the underlying
        // D2D/DXGI resources.
        //

        DrawingSessionAdapterFixture f;

        auto canvasSwapChain = f.CreateTestSwapChain();

        ComPtr<ICanvasDrawingSession> drawingSession;
        Color canvasBlue = { 255, 0, 0, 255 };
        ThrowIfFailed(canvasSwapChain->CreateDrawingSession(canvasBlue, &drawingSession));
    }

    TEST_METHOD_EX(CanvasSwapChainDrawingSessionAdapter_EndDraw_Cleanup)
    {
        //
        // This test ensures that even if an exception occurs, EndDraw still gets called on
        // the native device context.
        //
        // This tests injects the failure a bit differently from 
        // CanvasImageSourceDrawingSessionAdapter_When_SisNative_Gives_Unusuable_DeviceContext_Then_EndDraw_Called.
        //
        // The implementation for CanvasImageSource does a QI with a change in type (ID2D1DeviceContext/ID2D1DeviceContext1)
        // so that test injects a failure into CopyTo. CanvasSwapChain's implementation does not have this QI, so
        // the failure is injected into Clear instead.
        // 

        DrawingSessionAdapterFixture f(DrawingSessionAdapterFixture::FailDuringClear);

        auto canvasSwapChain = f.CreateTestSwapChain();

        ComPtr<ICanvasDrawingSession> drawingSession;
        Assert::AreEqual(E_NOTIMPL, canvasSwapChain->CreateDrawingSession(Color{0, 0, 0, 0}, &drawingSession));
    }
};