#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace hlclient::renderer {
// Project-authored pixel font. ASCII, Ukrainian alphabet and common Cyrillic
// letters are drawings, not extracted or redistributed system/Valve fonts.
using ScreenUiGlyph=std::array<std::uint8_t,9>;
[[nodiscard]] constexpr ScreenUiGlyph screen_ui_glyph(char32_t c) noexcept {
    switch(c) {
    case U'A':case U'А':return {14,17,17,31,17,17,17};
    case U'B':case U'В':return {30,17,17,30,17,17,30};
    case U'C':case U'С':return {15,16,16,16,16,16,15};
    case U'D':return {30,17,17,17,17,17,30};
    case U'E':case U'Е':return {31,16,16,30,16,16,31};
    case U'F':return {31,16,16,30,16,16,16};
    case U'G':return {15,16,16,23,17,17,15};
    case U'H':case U'Н':return {17,17,17,31,17,17,17};
    case U'I':case U'І':return {14,4,4,4,4,4,14};
    case U'J':return {7,2,2,2,18,18,12};
    case U'K':case U'К':return {17,18,20,24,20,18,17};
    case U'L':return {16,16,16,16,16,16,31};
    case U'M':case U'М':return {17,27,21,21,17,17,17};
    case U'N':return {17,25,21,19,17,17,17};
    case U'O':case U'О':return {14,17,17,17,17,17,14};
    case U'P':case U'Р':return {30,17,17,30,16,16,16};
    case U'Q':return {14,17,17,17,21,18,13};
    case U'R':return {30,17,17,30,20,18,17};
    case U'S':return {15,16,16,14,1,1,30};
    case U'T':case U'Т':return {31,4,4,4,4,4,4};
    case U'U':return {17,17,17,17,17,17,14};
    case U'V':return {17,17,17,17,17,10,4};
    case U'W':return {17,17,17,21,21,21,10};
    case U'X':case U'Х':return {17,17,10,4,10,17,17};
    case U'Y':return {17,17,10,4,4,4,4};
    case U'Z':return {31,1,2,4,8,16,31};
    case U'a':case U'а':return {0,0,14,1,15,17,15};
    case U'b':return {16,16,30,17,17,17,30};
    case U'c':case U'с':return {0,0,15,16,16,16,15};
    case U'd':return {1,1,15,17,17,17,15};
    case U'e':case U'е':return {0,0,14,17,31,16,15};
    case U'f':return {6,9,8,28,8,8,8};
    case U'g':return {0,0,15,17,17,17,15,1,14};
    case U'h':return {16,16,30,17,17,17,17};
    case U'i':case U'і':return {4,0,12,4,4,4,14};
    case U'j':return {2,0,6,2,2,2,2,18,12};
    case U'k':case U'к':return {16,16,17,18,28,18,17};
    case U'l':return {12,4,4,4,4,4,14};
    case U'm':return {0,0,26,21,21,21,21};
    case U'n':return {0,0,30,17,17,17,17};
    case U'o':case U'о':return {0,0,14,17,17,17,14};
    case U'p':case U'р':return {0,0,30,17,17,17,30,16,16};
    case U'q':return {0,0,15,17,17,17,15,1,1};
    case U'r':return {0,0,22,25,16,16,16};
    case U's':return {0,0,15,16,14,1,30};
    case U't':return {8,8,28,8,8,9,6};
    case U'u':return {0,0,17,17,17,19,13};
    case U'v':return {0,0,17,17,17,10,4};
    case U'w':return {0,0,17,17,21,21,10};
    case U'x':case U'х':return {0,0,17,10,4,10,17};
    case U'y':case U'у':return {0,0,17,17,17,17,15,1,14};
    case U'z':return {0,0,31,2,4,8,31};
    case U'Б':return {31,16,16,30,17,17,30};
    case U'Г':return {31,16,16,16,16,16,16};
    case U'Ґ':return {1,31,16,16,16,16,16};
    case U'Д':return {6,10,10,10,10,17,31,17};
    case U'Є':return {15,16,16,30,16,16,15};
    case U'Ж':return {21,21,14,4,14,21,21};
    case U'З':return {30,1,1,14,1,1,30};
    case U'И':return {17,17,19,21,25,17,17};
    case U'Ї':return {10,0,14,4,4,4,14};
    case U'Й':return {10,4,17,19,21,25,17};
    case U'Л':return {7,9,9,9,9,9,17};
    case U'П':return {31,17,17,17,17,17,17};
    case U'У':return {17,17,17,15,1,1,14};
    case U'Ф':return {4,14,21,21,21,14,4};
    case U'Ц':return {18,18,18,18,18,18,31,1};
    case U'Ч':return {17,17,17,15,1,1,1};
    case U'Ш':return {21,21,21,21,21,21,31};
    case U'Щ':return {21,21,21,21,21,21,31,1};
    case U'Ь':return {16,16,16,30,17,17,30};
    case U'Ю':return {18,21,21,29,21,21,18};
    case U'Я':return {15,17,17,15,5,9,17};
    case U'б':return {6,8,16,30,17,17,14};
    case U'в':return {0,0,30,17,30,17,30};
    case U'г':return {0,0,31,16,16,16,16};
    case U'ґ':return {0,1,31,16,16,16,16};
    case U'д':return {0,0,6,10,10,17,31,17};
    case U'є':return {0,0,15,16,28,16,15};
    case U'ж':return {0,0,21,21,14,21,21};
    case U'з':return {0,0,30,1,14,1,30};
    case U'и':return {0,0,17,19,21,25,17};
    case U'ї':return {10,0,12,4,4,4,14};
    case U'й':return {10,4,17,19,21,25,17};
    case U'л':return {0,0,7,9,9,9,17};
    case U'м':return {0,0,17,27,21,17,17};
    case U'н':return {0,0,17,17,31,17,17};
    case U'п':return {0,0,31,17,17,17,17};
    case U'т':return {0,0,31,4,4,4,4};
    case U'ф':return {4,4,14,21,21,14,4,4,4};
    case U'ц':return {0,0,18,18,18,18,31,1};
    case U'ч':return {0,0,17,17,15,1,1};
    case U'ш':return {0,0,21,21,21,21,31};
    case U'щ':return {0,0,21,21,21,21,31,1};
    case U'ь':return {0,0,16,16,30,17,30};
    case U'ю':return {0,0,18,21,29,21,18};
    case U'я':return {0,0,15,17,15,5,25};
    case U'Ё':return {10,0,31,16,30,16,31};
    case U'ё':return {10,0,14,17,31,16,15};
    case U'Ы':return {17,17,17,25,21,21,25};
    case U'ы':return {0,0,17,17,25,21,25};
    case U'Э':return {30,1,1,15,1,1,30};
    case U'э':return {0,0,30,1,7,1,30};
    case U'Ъ':return {24,8,8,14,9,9,14};
    case U'ъ':return {0,0,24,8,14,9,14};
    case U'λ':return {24,8,4,4,10,10,19};
    case U'0':return {14,17,19,21,25,17,14};
    case U'1':return {4,12,4,4,4,4,14};
    case U'2':return {14,17,1,2,4,8,31};
    case U'3':return {30,1,1,14,1,1,30};
    case U'4':return {2,6,10,18,31,2,2};
    case U'5':return {31,16,16,30,1,1,30};
    case U'6':return {14,16,16,30,17,17,14};
    case U'7':return {31,1,2,4,8,8,8};
    case U'8':return {14,17,17,14,17,17,14};
    case U'9':return {14,17,17,15,1,1,14};
    case U'!':return {4,4,4,4,4,0,4};
    case U'"':return {10,10,10};
    case U'#':return {10,31,10,10,31,10};
    case U'$':return {4,15,20,14,5,30,4};
    case U'%':return {25,25,2,4,8,19,19};
    case U'&':return {12,18,20,8,21,18,13};
    case U'\'':case U'’':return {4,4,8};
    case U'(':return {2,4,8,8,8,4,2};
    case U')':return {8,4,2,2,2,4,8};
    case U'*':return {0,21,14,31,14,21};
    case U'+':return {0,4,4,31,4,4};
    case U',':return {0,0,0,0,0,4,4,8};
    case U'-':case U'–':case U'—':return {0,0,0,31};
    case U'.':return {0,0,0,0,0,0,4};
    case U'/':return {1,1,2,4,8,16,16};
    case U':':return {0,4,4,0,4,4};
    case U';':return {0,4,4,0,4,4,8};
    case U'<':return {1,2,4,8,4,2,1};
    case U'=':return {0,0,31,0,31};
    case U'>':return {16,8,4,2,4,8,16};
    case U'@':return {14,17,23,21,23,16,14};
    case U'[':return {14,8,8,8,8,8,14};
    case U'\\':return {16,16,8,4,2,1,1};
    case U']':return {14,2,2,2,2,2,14};
    case U'^':return {4,10,17};
    case U'_':return {0,0,0,0,0,0,31};
    case U'`':return {8,4,2};
    case U'{':return {2,4,4,8,4,4,2};
    case U'|':return {4,4,4,4,4,4,4};
    case U'}':return {8,4,4,2,4,4,8};
    case U'~':return {0,0,9,22};
    case U' ':case U'\t':return {};
    default:return {14,17,1,2,4,0,4};
    }
}

[[nodiscard]] inline char32_t next_screen_ui_codepoint(std::string_view text,std::size_t& offset) noexcept {
    if(offset>=text.size())return 0;
    const auto first=static_cast<unsigned char>(text[offset++]);
    if(first<0x80)return first;
    unsigned following{};char32_t value{},minimum{};
    if(first>=0xc2&&first<=0xdf){following=1;value=first&31U;minimum=0x80;}
    else if(first>=0xe0&&first<=0xef){following=2;value=first&15U;minimum=0x800;}
    else if(first>=0xf0&&first<=0xf4){following=3;value=first&7U;minimum=0x10000;}
    else return 0xfffd;
    if(following>text.size()-offset)return 0xfffd;
    for(unsigned i=0;i<following;++i){const auto c=static_cast<unsigned char>(text[offset+i]);
        if((c&0xc0U)!=0x80U)return 0xfffd;value=(value<<6U)|(c&63U);}
    offset+=following;
    return value<minimum||value>0x10ffff||(value>=0xd800&&value<=0xdfff)?0xfffd:value;
}
} // namespace hlclient::renderer
