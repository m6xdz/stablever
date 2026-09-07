#pragma once
#include <d3d11.h>
#include <wincodec.h>
#include <vector>
#include <string>
#include <wrl/client.h>
#include <objbase.h>

#pragma comment( lib, "Windowscodecs.lib" )

namespace util {
    inline ID3D11ShaderResourceView* load_texture_from_memory(ID3D11Device* device,const std::string& bytes) {
        if(!device||bytes.empty()||bytes.size()>1024*1024)return nullptr;
        struct com_scope { HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);~com_scope(){if(SUCCEEDED(hr))CoUninitialize();} } scope;
        Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
        Microsoft::WRL::ComPtr<IWICStream> stream;
        Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))||
           FAILED(factory->CreateStream(&stream))||
           FAILED(stream->InitializeFromMemory(reinterpret_cast<BYTE*>(const_cast<char*>(bytes.data())),static_cast<DWORD>(bytes.size())))||
           FAILED(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnDemand,&decoder))||
           FAILED(decoder->GetFrame(0,&frame)))return nullptr;
        UINT w=0,h=0;
        if(FAILED(frame->GetSize(&w,&h))||!w||!h||w>512||h>512)return nullptr;
        if(FAILED(factory->CreateFormatConverter(&converter))||FAILED(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return nullptr;
        std::vector<BYTE> pixels(w*h*4);
        if(FAILED(converter->CopyPixels(nullptr,w*4,static_cast<UINT>(pixels.size()),pixels.data())))return nullptr;
        D3D11_TEXTURE2D_DESC desc{};desc.Width=w;desc.Height=h;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init{};init.pSysMem=pixels.data();init.SysMemPitch=w*4;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        if(FAILED(device->CreateTexture2D(&desc,&init,&texture)))return nullptr;
        ID3D11ShaderResourceView* result=nullptr;
        if(FAILED(device->CreateShaderResourceView(texture.Get(),nullptr,&result)))return nullptr;
        return result;
    }
    inline ID3D11ShaderResourceView* load_texture_from_file( ID3D11Device* device, const std::wstring& file_path, int& out_width, int& out_height ) {
        HRESULT hr_co = CoInitializeEx( nullptr, COINIT_APARTMENTTHREADED );
        bool co_init = ( hr_co == S_OK || hr_co == S_FALSE );

        Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
        HRESULT hr = CoCreateInstance( CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS( &factory ) );
        if ( FAILED( hr ) ) {
            if ( co_init ) CoUninitialize( );
            return nullptr;
        }

        Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        hr = factory->CreateDecoderFromFilename( file_path.c_str( ), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder );
        if ( FAILED( hr ) ) {
            if ( co_init ) CoUninitialize( );
            return nullptr;
        }

        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        hr = decoder->GetFrame( 0, &frame );
        if ( FAILED( hr ) ) {
            if ( co_init ) CoUninitialize( );
            return nullptr;
        }

        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        hr = factory->CreateFormatConverter( &converter );
        if ( FAILED( hr ) ) {
            if ( co_init ) CoUninitialize( );
            return nullptr;
        }

        hr = converter->Initialize( frame.Get( ), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeCustom );
        if ( FAILED( hr ) ) {
            if ( co_init ) CoUninitialize( );
            return nullptr;
        }

        UINT width = 0, height = 0;
        hr = converter->GetSize( &width, &height );
        if ( FAILED( hr ) ) {
            if ( co_init ) CoUninitialize( );
            return nullptr;
        }

        out_width = static_cast<int>( width );
        out_height = static_cast<int>( height );

        std::vector<uint8_t> buffer( width * height * 4 );
        hr = converter->CopyPixels( nullptr, width * 4, static_cast<UINT>( buffer.size( ) ), buffer.data( ) );
        if ( FAILED( hr ) ) {
            if ( co_init ) CoUninitialize( );
            return nullptr;
        }

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA init_data{};
        init_data.pSysMem = buffer.data( );
        init_data.SysMemPitch = width * 4;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        hr = device->CreateTexture2D( &desc, &init_data, &texture );
        if ( FAILED( hr ) ) {
            if ( co_init ) CoUninitialize( );
            return nullptr;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
        srv_desc.Format = desc.Format;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels = 1;

        ID3D11ShaderResourceView* srv = nullptr;
        hr = device->CreateShaderResourceView( texture.Get( ), &srv_desc, &srv );
        if ( co_init ) CoUninitialize( );
        if ( FAILED( hr ) ) return nullptr;

        return srv;
    }
}