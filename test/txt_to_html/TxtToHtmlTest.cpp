#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "Print.h"
#include "TxtToHtml.h"
#include "expat.h"

namespace {

class StringPrint : public Print {
 public:
  std::string str;
  size_t write(uint8_t b) override {
    str.push_back(static_cast<char>(b));
    return 1;
  }
  size_t write(const uint8_t* buffer, size_t size) override {
    str.append(reinterpret_cast<const char*>(buffer), size);
    return size;
  }
};

const std::string kHeader =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<!-- TXT_CACHE_VERSION: 3 -->\n"
    "<!DOCTYPE html>\n<html>\n<head><title>test</title></head>\n<body>\n";
const std::string kFooter = "\n</body>\n</html>\n";

std::string convert(std::string_view content) {
  StringPrint out;
  EXPECT_TRUE(TxtToHtml::stream("test.txt", content, out));
  return out.str;
}

// std::string from a byte literal that may contain NUL bytes (UTF-16 data).
template <size_t N>
std::string bytes(const char (&s)[N]) {
  return std::string(s, N - 1);
}

// The converted HTML declares encoding="utf-8" and must always parse with a
// strict XML parser, whatever encoding the source TXT used.
void expectParsesAsXml(const std::string& html) {
  XML_Parser parser = XML_ParserCreate(nullptr);
  const XML_Status status = XML_Parse(parser, html.data(), static_cast<int>(html.size()), XML_TRUE);
  if (status == XML_STATUS_ERROR) {
    ADD_FAILURE() << "expat: " << XML_ErrorString(XML_GetErrorCode(parser)) << " at line "
                  << XML_GetCurrentLineNumber(parser);
  }
  XML_ParserFree(parser);
}

std::string body(std::string_view content) {
  const std::string html = convert(content);
  expectParsesAsXml(html);
  EXPECT_GE(html.size(), kHeader.size() + kFooter.size());
  EXPECT_EQ(html.compare(0, kHeader.size(), kHeader), 0);
  EXPECT_EQ(html.compare(html.size() - kFooter.size(), kFooter.size(), kFooter), 0);
  return html.substr(kHeader.size(), html.size() - kHeader.size() - kFooter.size());
}

TEST(TxtToHtmlTest, LeadingSpacesIndentation) {
  EXPECT_EQ(convert("  two spaces"), kHeader + "&#160;&#160;two spaces" + kFooter);
  EXPECT_EQ(convert("    four spaces"), kHeader + "&#160;&#160;&#160;&#160;four spaces" + kFooter);
  EXPECT_EQ(convert("Line 1\n   three spaces"), kHeader + "Line 1<br />&#160;&#160;&#160;three spaces" + kFooter);
}

TEST(TxtToHtmlTest, MidLineConsecutiveSpaces) {
  EXPECT_EQ(convert("One space"), kHeader + "One space" + kFooter);
  EXPECT_EQ(convert("Two  spaces"), kHeader + "Two&#160; spaces" + kFooter);
  EXPECT_EQ(convert("Three   spaces"), kHeader + "Three&#160;&#160; spaces" + kFooter);
  EXPECT_EQ(convert("Four    spaces"), kHeader + "Four&#160;&#160;&#160; spaces" + kFooter);
}

TEST(TxtToHtmlTest, PreservesCacheVersionTagsForBothFormats) {
  StringPrint out;
  ASSERT_TRUE(TxtToHtml::stream("test.MD", "text", out));
  EXPECT_EQ(out.str,
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<!-- MD_CACHE_VERSION: 3 -->\n"
            "<!DOCTYPE html>\n<html>\n<head><title>test</title></head>\n<body>\ntext" +
                kFooter);
  EXPECT_NE(out.str.find(TxtToHtml::cacheVersionTag("test.MD")), std::string::npos);
  EXPECT_NE(convert("text").find(TxtToHtml::cacheVersionTag("test.txt")), std::string::npos);
}

TEST(TxtToHtmlTest, AsciiContentUnchanged) {
  EXPECT_EQ(convert("plain ascii & <tags>\ttab"), kHeader + "plain ascii &amp; &lt;tags&gt;\ttab" + kFooter);
}

TEST(TxtToHtmlTest, Utf8PassthroughWithAndWithoutBom) {
  const std::string utf8 = "\xE4\xBD\xA0\xE5\xA5\xBD\xE4\xB8\x96\xE7\x95\x8C";  // 你好世界
  EXPECT_EQ(body(utf8), utf8);
  EXPECT_EQ(body("\xEF\xBB\xBF" + utf8), utf8);
}

TEST(TxtToHtmlTest, GbkChineseConvertsToUtf8) {
  // "你好世界\n第二章 开始" in GBK.
  const std::string gbk = "\xC4\xE3\xBA\xC3\xCA\xC0\xBD\xE7\n\xB5\xDA\xB6\xFE\xD5\xC2 \xBF\xAA\xCA\xBC";
  const std::string utf8 =
      "\xE4\xBD\xA0\xE5\xA5\xBD\xE4\xB8\x96\xE7\x95\x8C<br />"
      "\xE7\xAC\xAC\xE4\xBA\x8C\xE7\xAB\xA0 \xE5\xBC\x80\xE5\xA7\x8B";
  EXPECT_EQ(body(gbk), utf8);
}

TEST(TxtToHtmlTest, Utf8BomFollowedByGbkBodyStillConverts) {
  // Some tools prepend a UTF-8 BOM to GBK text; the BOM must not lock UTF-8
  // mode or every GBK byte would degrade to U+FFFD.
  const std::string gbk = "\xC4\xE3\xBA\xC3\xCA\xC0\xBD\xE7";  // 你好世界
  EXPECT_EQ(body("\xEF\xBB\xBF" + gbk), "\xE4\xBD\xA0\xE5\xA5\xBD\xE4\xB8\x96\xE7\x95\x8C");
}

TEST(TxtToHtmlTest, GbkFullWidthPunctuationIsNotEscaped) {
  // GBK A3BC/A3BE/A3A6 are full-width ＜＞＆ (U+FF1C/U+FF1E/U+FF06), not ASCII
  // markup characters, so they must survive unescaped as UTF-8.
  const std::string gbk = "\xA3\xBC\xA3\xBE\xA3\xA6";
  EXPECT_EQ(body(gbk), "\xEF\xBC\x9C\xEF\xBC\x9E\xEF\xBC\x86");
}

TEST(TxtToHtmlTest, GbkEscapesAsciiMarkupCharacters) {
  const std::string gbk = "\xC4\xE3&<> \xBA\xC3";  // 你&<> 好
  EXPECT_EQ(body(gbk), "\xE4\xBD\xA0&amp;&lt;&gt; \xE5\xA5\xBD");
}

TEST(TxtToHtmlTest, GbkPairAcrossReadBufferBoundary) {
  // First bytes are GBK so detection picks GBK mode, then a double-byte pair
  // straddles the 8192-byte read buffer boundary.
  for (const size_t padding : {8188, 8189, 8190, 8191, 8192}) {
    std::string input = "\xC4\xE3";  // 你 -> immediate invalid UTF-8 -> GBK mode
    input.append(padding, 'a');
    input += "\xBA\xC3";          // 好
    input += "\xCA\xC0\xBD\xE7";  // 世界
    std::string expected = "\xE4\xBD\xA0" + std::string(padding, 'a') + "\xE5\xA5\xBD\xE4\xB8\x96\xE7\x95\x8C";
    EXPECT_EQ(body(input), expected) << "padding=" << padding;
  }
}

TEST(TxtToHtmlTest, GbkInvalidTrailBecomesReplacementChar) {
  // 0xC4 is a GBK lead byte but 0x21 is not a valid trail byte.
  const std::string input = "\xC4\xE3\xC4\x21\xBA\xC3";  // 你 <bad> 好
  EXPECT_EQ(body(input), "\xE4\xBD\xA0\xEF\xBF\xBD!\xE5\xA5\xBD");
}

TEST(TxtToHtmlTest, Utf16LeBomConvertsToUtf8) {
  const std::string utf16 =
      bytes("\xFF\xFE\x60\x4F\x7D\x59\x16\x4E\x4C\x75\x0A\x00\x2C\x7B\x8C\x4E");  // 你好世界\n第二
  EXPECT_EQ(body(utf16), "\xE4\xBD\xA0\xE5\xA5\xBD\xE4\xB8\x96\xE7\x95\x8C<br />\xE7\xAC\xAC\xE4\xBA\x8C");
}

TEST(TxtToHtmlTest, Utf16BeBomConvertsToUtf8) {
  const std::string utf16 = bytes("\xFE\xFF\x4F\x60\x59\x7D\x4E\x16\x75\x4C\x00\x0A");  // 你好世界\n
  EXPECT_EQ(body(utf16), "\xE4\xBD\xA0\xE5\xA5\xBD\xE4\xB8\x96\xE7\x95\x8C<br />");
}

TEST(TxtToHtmlTest, Utf16SurrogatePairConvertsToUtf8) {
  // U+1F600 (😀) is D83D DE00 in UTF-16.
  EXPECT_EQ(body(bytes("\xFF\xFE\x3D\xD8\x00\xDE")), "\xF0\x9F\x98\x80");
  EXPECT_EQ(body(bytes("\xFE\xFF\xD8\x3D\xDE\x00")), "\xF0\x9F\x98\x80");
}

TEST(TxtToHtmlTest, Utf16UnpairedSurrogateBecomesReplacementChar) {
  EXPECT_EQ(body(bytes("\xFF\xFE\x3D\xD8\x41\x00")),
            "\xEF\xBF\xBD"
            "A");
  EXPECT_EQ(body(bytes("\xFF\xFE\x00\xDE")), "\xEF\xBF\xBD");
  EXPECT_EQ(body(bytes("\xFF\xFE\x3D\xD8")), "\xEF\xBF\xBD");
}

TEST(TxtToHtmlTest, Utf16SurrogatePairAcrossReadBufferBoundary) {
  // Padding in whole UTF-16 units (a = 61 00) places the surrogate pair
  // (😀 = 3D D8 00 DE) at different alignments around the 8192-byte read
  // buffer boundary, including the high/low split across two reads.
  for (const size_t padding : {4093, 4094, 4095}) {
    std::string input = "\xFF\xFE";
    for (size_t i = 0; i < padding; i++) input += bytes("\x61\x00");
    input += bytes("\x3D\xD8\x00\xDE");  // U+1F600
    input += bytes("\x60\x4F");          // 你
    std::string expected = std::string(padding, 'a') + "\xF0\x9F\x98\x80\xE4\xBD\xA0";
    EXPECT_EQ(body(input), expected) << "padding=" << padding;
  }
}

TEST(TxtToHtmlTest, Utf16OddByteAtEofBecomesReplacementChar) {
  EXPECT_EQ(body(bytes("\xFF\xFE\x41\x00\x42")), "A\xEF\xBF\xBD");
}

TEST(TxtToHtmlTest, Utf16EscapesAsciiMarkupCharacters) {
  EXPECT_EQ(body(bytes("\xFF\xFE\x26\x00\x3C\x00\x3E\x00")), "&amp;&lt;&gt;");
}

TEST(TxtToHtmlTest, Utf8InvalidBytesAfterSniffWindowBecomeReplacementChar) {
  // The first 8192 bytes are valid UTF-8, locking UTF-8 mode; later invalid
  // bytes must be sanitized to U+FFFD so the output stays parseable.
  std::string input(8192, 'a');
  input += '\xFF';
  input += 'b';
  EXPECT_EQ(body(input), std::string(8192, 'a') + "\xEF\xBF\xBD" + "b");
}

TEST(TxtToHtmlTest, Utf8SequenceAcrossReadBufferBoundary) {
  // A 3-byte UTF-8 sequence starts at the last byte of the sniff buffer.
  std::string input(8191, 'a');
  input += "\xE4\xBD\xA0";  // 你
  EXPECT_EQ(body(input), std::string(8191, 'a') + "\xE4\xBD\xA0");
}

TEST(TxtToHtmlTest, Utf8TruncatedSequenceAtEofBecomesReplacementChar) {
  std::string input(9000, 'a');
  input += "\xE4\xBD";  // truncated 你
  EXPECT_EQ(body(input), std::string(9000, 'a') + "\xEF\xBF\xBD");
}

TEST(TxtToHtmlTest, EmptyAndTinyInputs) {
  EXPECT_EQ(body(""), "");
  EXPECT_EQ(body("a"), "a");
  EXPECT_EQ(body("\xEF"), "\xEF\xBF\xBD");      // lone byte, no BOM
  EXPECT_EQ(body("\xEF\xBB"), "\xEF\xBF\xBD");  // truncated UTF-8 BOM
  EXPECT_EQ(body(std::string("\xFF")), "\xEF\xBF\xBD");
  EXPECT_EQ(body(std::string("\xFF\xFE")), "");  // bare UTF-16 LE BOM
}

}  // namespace
