#include "TxtToHtml.h"

#include <FsHelpers.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>

#include "GbkUnicodeTable.h"

const char* TxtToHtml::cacheVersionTag(std::string_view filename) {
  return FsHelpers::hasMarkdownExtension(filename) ? "<!-- MD_CACHE_VERSION: 3 -->" : "<!-- TXT_CACHE_VERSION: 3 -->";
}

namespace {

enum class InputEncoding : uint8_t { Utf8, Gbk, Utf16Le, Utf16Be };

// Length of the UTF-8 sequence starting with `lead`, or 0 if it cannot start one.
int utf8SeqLen(uint8_t lead) {
  if (lead < 0x80) return 1;
  if (lead >= 0xC2 && lead <= 0xDF) return 2;
  if (lead >= 0xE0 && lead <= 0xEF) return 3;
  if (lead >= 0xF0 && lead <= 0xF4) return 4;
  return 0;
}

// Validates the continuation bytes seq[1..avail) of a sequence of total length
// `total` (avail may be < total for a partial sequence). Enforces the RFC 3629
// ranges that reject overlong forms, surrogates, and values above U+10FFFF.
bool utf8TailValid(const uint8_t* seq, size_t avail, int total) {
  if (avail < 2) return true;
  uint8_t lo = 0x80, hi = 0xBF;
  if (seq[0] == 0xE0)
    lo = 0xA0;
  else if (seq[0] == 0xED)
    hi = 0x9F;
  else if (seq[0] == 0xF0)
    lo = 0x90;
  else if (seq[0] == 0xF4)
    hi = 0x8F;
  if (seq[1] < lo || seq[1] > hi) return false;
  for (size_t k = 2; k < avail && k < static_cast<size_t>(total); k++) {
    if (seq[k] < 0x80 || seq[k] > 0xBF) return false;
  }
  return true;
}

uint32_t utf8DecodeCp(const uint8_t* p, int len) {
  switch (len) {
    case 1:
      return p[0];
    case 2:
      return ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
    case 3:
      return ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    default:
      return ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
  }
}

// No BOM: Chinese TXT files are commonly GBK, whose byte patterns break UTF-8
// validity quickly, so any invalid sequence in the sniff window selects GBK.
InputEncoding sniffEncoding(const uint8_t* data, size_t len) {
  size_t i = 0;
  while (i < len) {
    const int len1 = utf8SeqLen(data[i]);
    if (len1 == 0) return InputEncoding::Gbk;
    if (i + len1 > len) break;  // incomplete tail at window end: assume UTF-8
    if (!utf8TailValid(data + i, len1, len1)) return InputEncoding::Gbk;
    i += len1;
  }
  return InputEncoding::Utf8;
}

bool gbkIsTrail(uint8_t b) { return b >= 0x40 && b <= 0xFE && b != 0x7F; }

uint32_t gbkToUnicode(uint8_t lead, uint8_t trail) {
  const size_t index = (lead - 0x81) * 190 + (trail < 0x7F ? trail - 0x40 : trail - 0x41);
  return GBK_UNICODE_TABLE[index];
}

}  // namespace

bool TxtToHtml::stream(std::string_view filename, void* readerCtx, int (*readFn)(void* ctx, uint8_t* buf, size_t size),
                       Print& out) {
  constexpr size_t IN_BUF_SIZE = 8192;
  constexpr size_t OUT_BUF_SIZE = 8192;

  auto inBuf = makeUniqueNoThrow<uint8_t[]>(IN_BUF_SIZE);
  auto outBuf = makeUniqueNoThrow<uint8_t[]>(OUT_BUF_SIZE);
  if (!inBuf || !outBuf) {
    LOG_ERR("TXT", "OOM: TXT/MD HTML streaming buffers");
    return false;
  }

  size_t outPos = 0;
  bool outputOk = true;
  auto flushOut = [&]() {
    if (outPos > 0) {
      const size_t written = out.write(outBuf.get(), outPos);
      outputOk = outputOk && (written == outPos);
      outPos = 0;
    }
  };

  auto writeByte = [&](uint8_t b) {
    outBuf[outPos++] = b;
    if (outPos == OUT_BUF_SIZE) flushOut();
  };

  auto writeStr = [&](std::string_view s) {
    for (char c : s) {
      writeByte(static_cast<uint8_t>(c));
    }
  };

  writeStr("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
  writeStr(cacheVersionTag(filename));
  writeByte('\n');
  writeStr("<!DOCTYPE html>\n<html>\n<head><title>");
  std::string title = FsHelpers::getFileNameWithoutExtension(filename);
  for (char c : title) {
    if (c == '&')
      writeStr("&amp;");
    else if (c == '<')
      writeStr("&lt;");
    else if (c == '>')
      writeStr("&gt;");
    else
      writeByte(static_cast<uint8_t>(c));
  }
  writeStr("</title></head>\n<body>\n");

  bool atLineStart = true;
  size_t pendingSpaces = 0;

  // Single sink for decoded text in every input encoding: cp < 0x80 keeps the
  // original byte-level whitespace/escaping semantics, the rest is UTF-8.
  auto emitCodePoint = [&](uint32_t cp) {
    // XML 1.0 forbids U+FFFE/U+FFFF even though they are valid UTF-8/UTF-16.
    if (cp == 0xFFFE || cp == 0xFFFF) cp = 0xFFFD;

    if (cp == '\r') return;

    if (cp == '\n') {
      pendingSpaces = 0;
      writeStr("<br />");
      atLineStart = true;
      return;
    }

    if (cp == ' ') {
      if (atLineStart) {
        writeStr("&#160;");
      } else {
        pendingSpaces++;
      }
      return;
    }

    if (pendingSpaces > 0) {
      for (size_t s = 0; s < pendingSpaces - 1; s++) {
        writeStr("&#160;");
      }
      writeByte(' ');
      pendingSpaces = 0;
    }
    atLineStart = false;

    if (cp == '&') {
      writeStr("&amp;");
    } else if (cp == '<') {
      writeStr("&lt;");
    } else if (cp == '>') {
      writeStr("&gt;");
    } else if (cp < 0x20 && cp != '\t') {
      writeByte(' ');
    } else if (cp < 0x80) {
      writeByte(static_cast<uint8_t>(cp));
    } else if (cp < 0x800) {
      writeByte(0xC0 | (cp >> 6));
      writeByte(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      writeByte(0xE0 | (cp >> 12));
      writeByte(0x80 | ((cp >> 6) & 0x3F));
      writeByte(0x80 | (cp & 0x3F));
    } else {
      writeByte(0xF0 | (cp >> 18));
      writeByte(0x80 | ((cp >> 12) & 0x3F));
      writeByte(0x80 | ((cp >> 6) & 0x3F));
      writeByte(0x80 | (cp & 0x3F));
    }
  };

  // Fill one whole buffer before choosing an encoding: UTF-8 without BOM is
  // distinguished from GBK by scanning this window for invalid sequences.
  int bytesRead = 0;
  size_t detectLen = 0;
  while (detectLen < IN_BUF_SIZE &&
         (bytesRead = readFn(readerCtx, inBuf.get() + detectLen, IN_BUF_SIZE - detectLen)) > 0) {
    detectLen += static_cast<size_t>(bytesRead);
  }
  if (bytesRead < 0) {
    LOG_ERR("TXT", "Read error while streaming TXT/MD: %.*s", static_cast<int>(filename.size()), filename.data());
    return false;
  }

  InputEncoding encoding = InputEncoding::Utf8;
  size_t contentStart = 0;
  if (detectLen >= 3 && inBuf[0] == 0xEF && inBuf[1] == 0xBB && inBuf[2] == 0xBF) {
    contentStart = 3;
    // A UTF-8 BOM does not prove the body is UTF-8: tools exist that prepend
    // one to GBK text, so the body still goes through the sniff.
    encoding = sniffEncoding(inBuf.get() + contentStart, detectLen - contentStart);
  } else if (detectLen >= 2 && inBuf[0] == 0xFF && inBuf[1] == 0xFE) {
    encoding = InputEncoding::Utf16Le;
    contentStart = 2;
  } else if (detectLen >= 2 && inBuf[0] == 0xFE && inBuf[1] == 0xFF) {
    encoding = InputEncoding::Utf16Be;
    contentStart = 2;
  } else {
    encoding = sniffEncoding(inBuf.get(), detectLen);
  }

  // Multi-byte units split across read boundaries are completed from carry.
  uint8_t carry[4];
  size_t carryLen = 0;
  uint16_t utf16HighSurrogate = 0;
  bool utf16HasHighSurrogate = false;

  auto emitUtf16Unit = [&](uint16_t unit) {
    if (utf16HasHighSurrogate) {
      utf16HasHighSurrogate = false;
      if (unit >= 0xDC00 && unit <= 0xDFFF) {
        emitCodePoint(0x10000u + ((utf16HighSurrogate - 0xD800u) << 10) + (unit - 0xDC00u));
        return;
      }
      emitCodePoint(0xFFFD);  // unpaired high surrogate; `unit` is handled below
    }
    if (unit >= 0xD800 && unit <= 0xDBFF) {
      utf16HighSurrogate = unit;
      utf16HasHighSurrogate = true;
    } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
      emitCodePoint(0xFFFD);
    } else {
      emitCodePoint(unit);
    }
  };

  auto processChunk = [&](const uint8_t* data, size_t len) {
    switch (encoding) {
      case InputEncoding::Utf8: {
        size_t i = 0;
        if (carryLen > 0) {
          // carry[] always holds a valid prefix of a sequence of this length.
          const int total = utf8SeqLen(carry[0]);
          const size_t need = static_cast<size_t>(total) - carryLen;
          if (len < need) {
            uint8_t seq[4];
            memcpy(seq, carry, carryLen);
            memcpy(seq + carryLen, data, len);
            if (utf8TailValid(seq, carryLen + len, total)) {
              memcpy(carry, seq, carryLen + len);
              carryLen += len;
              return;
            }
            emitCodePoint(0xFFFD);
            carryLen = 0;
            // Reprocess the whole chunk below.
          } else {
            uint8_t seq[4];
            memcpy(seq, carry, carryLen);
            memcpy(seq + carryLen, data, need);
            if (utf8TailValid(seq, total, total)) {
              emitCodePoint(utf8DecodeCp(seq, total));
              i = need;
            } else {
              emitCodePoint(0xFFFD);
            }
            carryLen = 0;
          }
        }
        while (i < len) {
          const int seqLen = utf8SeqLen(data[i]);
          if (seqLen == 0) {
            emitCodePoint(0xFFFD);
            i++;
            continue;
          }
          if (i + static_cast<size_t>(seqLen) > len) {
            if (utf8TailValid(data + i, len - i, seqLen)) {
              memcpy(carry, data + i, len - i);
              carryLen = len - i;
            } else {
              emitCodePoint(0xFFFD);
            }
            return;
          }
          if (seqLen > 1 && !utf8TailValid(data + i, seqLen, seqLen)) {
            emitCodePoint(0xFFFD);
            i++;
            continue;
          }
          emitCodePoint(utf8DecodeCp(data + i, seqLen));
          i += seqLen;
        }
        break;
      }
      case InputEncoding::Gbk: {
        size_t i = 0;
        if (carryLen > 0 && len > 0) {
          if (gbkIsTrail(data[0])) {
            emitCodePoint(gbkToUnicode(carry[0], data[0]));
            i = 1;
          } else {
            emitCodePoint(0xFFFD);
          }
          carryLen = 0;
        }
        while (i < len) {
          const uint8_t b = data[i];
          if (b < 0x80) {
            emitCodePoint(b);
            i++;
          } else if (b >= 0x81 && b <= 0xFE) {
            if (i + 1 < len) {
              if (gbkIsTrail(data[i + 1])) {
                emitCodePoint(gbkToUnicode(b, data[i + 1]));
                i += 2;
              } else {
                emitCodePoint(0xFFFD);
                i++;
              }
            } else {
              carry[0] = b;
              carryLen = 1;
              i++;
            }
          } else {
            emitCodePoint(0xFFFD);
            i++;
          }
        }
        break;
      }
      case InputEncoding::Utf16Le:
      case InputEncoding::Utf16Be: {
        const bool le = encoding == InputEncoding::Utf16Le;
        auto readUnit = [&](uint8_t a, uint8_t b) -> uint16_t {
          return le ? static_cast<uint16_t>((b << 8) | a) : static_cast<uint16_t>((a << 8) | b);
        };
        size_t i = 0;
        if (carryLen > 0 && len > 0) {
          emitUtf16Unit(readUnit(carry[0], data[0]));
          i = 1;
          carryLen = 0;
        }
        for (; i + 1 < len; i += 2) {
          emitUtf16Unit(readUnit(data[i], data[i + 1]));
        }
        if (i < len) {
          carry[0] = data[i];
          carryLen = 1;
        }
        break;
      }
    }
  };

  processChunk(inBuf.get() + contentStart, detectLen - contentStart);
  while (bytesRead > 0) {
    bytesRead = readFn(readerCtx, inBuf.get(), IN_BUF_SIZE);
    if (bytesRead > 0) {
      processChunk(inBuf.get(), static_cast<size_t>(bytesRead));
    }
  }
  if (bytesRead < 0) {
    LOG_ERR("TXT", "Read error while streaming TXT/MD: %.*s", static_cast<int>(filename.size()), filename.data());
    return false;
  }

  if (carryLen > 0) {
    emitCodePoint(0xFFFD);  // truncated multi-byte unit at end of input
  }
  if (utf16HasHighSurrogate) {
    emitCodePoint(0xFFFD);  // unpaired high surrogate at end of input
  }

  writeStr("\n</body>\n</html>\n");
  flushOut();
  if (!outputOk) {
    LOG_ERR("TXT", "Failed to stream complete HTML (write error or disk full)");
    return false;
  }
  return true;
}

bool TxtToHtml::stream(std::string_view filename, std::string_view content, Print& out) {
  struct ViewReader {
    std::string_view s;
    size_t pos = 0;
  } reader{content, 0};

  return stream(
      filename, &reader,
      [](void* ctx, uint8_t* buf, size_t size) -> int {
        auto* r = static_cast<ViewReader*>(ctx);
        if (r->pos >= r->s.size()) return 0;
        const size_t n = std::min(size, r->s.size() - r->pos);
        memcpy(buf, r->s.data() + r->pos, n);
        r->pos += n;
        return static_cast<int>(n);
      },
      out);
}
