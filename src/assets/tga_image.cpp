#include <hlclient/assets/tga_image.hpp>
#include <algorithm>
namespace hlclient::assets {
std::optional<ImageAsset> decode_tga_image(std::span<const std::byte> bytes) {
    if(bytes.size()<18 || bytes.size()>17U*1024U*1024U)return {};
    const auto at=[&](std::size_t i){return std::to_integer<unsigned>(bytes[i]);};
    const auto width=at(12)|(at(13)<<8),height=at(14)|(at(15)<<8);
    const auto type=at(2),bits=at(16),descriptor=at(17);
    if(at(1)!=0 || (type!=2 && type!=10) || (bits!=24 && bits!=32) ||
       !width || !height || width>2048 || height>2048 || (descriptor&0xc0)!=0 ||
       ((descriptor&15)!=0 && (descriptor&15)!=8) || (bits==24 && (descriptor&15)!=0))return {};
    std::size_t cursor=18+at(0),pixel=0;
    if(cursor>bytes.size())return {};
    ImageAsset result;result.width=width;result.height=height;
    result.pixels.resize(std::size_t(width)*height*4);
    const auto count=std::size_t(width)*height;
    const auto channels=bits/8;
    const auto write=[&](std::size_t index,const std::byte* input) {
        auto x=index%width,y=index/width;
        if(descriptor&16)x=width-1-x;
        if(!(descriptor&32))y=height-1-y;
        auto* output=result.pixels.data()+(y*width+x)*4;
        output[0]=input[2];output[1]=input[1];output[2]=input[0];
        output[3]=bits==32 && (descriptor&15)!=0 ? input[3] : std::byte{255};
    };
    while(pixel<count) {
        unsigned packet=0;
        if(type==10) {if(cursor>=bytes.size())return {};packet=at(cursor++);}
        const auto run=type==10 ? (packet&127)+1U : 1U;
        if(run>count-pixel)return {};
        if(type==10 && (packet&128)) {
            if(channels>bytes.size()-cursor)return {};
            for(unsigned i=0;i<run;++i)write(pixel++,bytes.data()+cursor);
            cursor+=channels;
        } else {
            if(std::size_t(run)*channels>bytes.size()-cursor)return {};
            for(unsigned i=0;i<run;++i) {write(pixel++,bytes.data()+cursor);cursor+=channels;}
        }
    }
    // Standard optional TGA footer/extension bytes are inert, never paths.
    return result;
}
}
