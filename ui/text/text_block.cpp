// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/text/text_block.h"

#include "styles/style_basic.h"
#include "base/debug_log.h"

#include <private/qfontengine_p.h>

// COPIED FROM qtextlayout.cpp AND MODIFIED
namespace Ui {
namespace Text {
namespace {

struct ScriptLine {
	int length = 0;
	QFixed textWidth;
};

// All members finished with "_" are internal.
struct LineBreakHelper {
	ScriptLine tmpData;
	ScriptLine spaceData;

	QGlyphLayout glyphs;

	int glyphCount = 0;
	int maxGlyphs = INT_MAX;
	int currentPosition = 0;

	glyph_t previousGlyph_ = 0;
	QFontEngine *previousFontEngine_ = nullptr;

	QFixed rightBearing;

	QFontEngine *fontEngine = nullptr;
	const unsigned short *logClusters = nullptr;

	glyph_t currentGlyph() const;
	void saveCurrentGlyph();
	void calculateRightBearing(QFontEngine *engine, glyph_t glyph);
	void calculateRightBearing();
	void calculateRightBearingForPreviousGlyph();

	// We always calculate the right bearing right before it is needed.
	// So we don't need caching / optimizations referred to delayed right bearing calculations.

	//static const QFixed RightBearingNotCalculated;

	//inline void resetRightBearing()
	//{
	//	rightBearing = RightBearingNotCalculated;
	//}

	// We express the negative right bearing as an absolute number
	// so that it can be applied to the width using addition.
	QFixed negativeRightBearing() const;

};

//const QFixed LineBreakHelper::RightBearingNotCalculated = QFixed(1);

glyph_t LineBreakHelper::currentGlyph() const {
	Q_ASSERT(currentPosition > 0);
	Q_ASSERT(logClusters[currentPosition - 1] < glyphs.numGlyphs);

	return glyphs.glyphs[logClusters[currentPosition - 1]];
}

void LineBreakHelper::saveCurrentGlyph() {
	if (currentPosition > 0
		&& logClusters[currentPosition - 1] < glyphs.numGlyphs) {
		// needed to calculate right bearing later
		previousGlyph_ = currentGlyph();
		previousFontEngine_ = fontEngine;
	} else {
		previousGlyph_ = 0;
		previousFontEngine_ = nullptr;
	}
}

void LineBreakHelper::calculateRightBearing(
		QFontEngine *engine,
		glyph_t glyph) {
	qreal rb;
	engine->getGlyphBearings(glyph, 0, &rb);

	// We only care about negative right bearings, so we limit the range
	// of the bearing here so that we can assume it's negative in the rest
	// of the code, as well ase use QFixed(1) as a sentinel to represent
	// the state where we have yet to compute the right bearing.
	rightBearing = qMin(QFixed::fromReal(rb), QFixed(0));
}

void LineBreakHelper::calculateRightBearing() {
	if (currentPosition > 0
		&& logClusters[currentPosition - 1] < glyphs.numGlyphs) {
		calculateRightBearing(fontEngine, currentGlyph());
	} else {
		rightBearing = 0;
	}
}

void LineBreakHelper::calculateRightBearingForPreviousGlyph() {
	if (previousGlyph_ > 0) {
		calculateRightBearing(previousFontEngine_, previousGlyph_);
	} else {
		rightBearing = 0;
	}
}

// We always calculate the right bearing right before it is needed.
// So we don't need caching / optimizations referred to delayed right bearing calculations.

//static const QFixed RightBearingNotCalculated;

//inline void resetRightBearing()
//{
//	rightBearing = RightBearingNotCalculated;
//}

// We express the negative right bearing as an absolute number
// so that it can be applied to the width using addition.
QFixed LineBreakHelper::negativeRightBearing() const {
	//if (rightBearing == RightBearingNotCalculated)
	//	return QFixed(0);

	return qAbs(rightBearing);
}

QString DebugCurrentParsingString, DebugCurrentParsingPart;
int DebugCurrentParsingFrom = 0;
int DebugCurrentParsingLength = 0;

void addNextCluster(
		int &pos,
		int end,
		ScriptLine &line,
		int &glyphCount,
		const QScriptItem &current,
		const unsigned short *logClusters,
		const QGlyphLayout &glyphs) {
	int glyphPosition = logClusters[pos];
	do { // got to the first next cluster
		++pos;
		++line.length;
	} while (pos < end && logClusters[pos] == glyphPosition);
	do { // calculate the textWidth for the rest of the current cluster.
		if (!glyphs.attributes[glyphPosition].dontPrint)
			line.textWidth += glyphs.advances[glyphPosition];
		++glyphPosition;
	} while (glyphPosition < current.num_glyphs
		&& !glyphs.attributes[glyphPosition].clusterStart);

	if (!((pos == end && glyphPosition == current.num_glyphs) || logClusters[pos] == glyphPosition)) {
		auto str = QStringList();
		for (auto i = 0; i < pos; ++i) {
			str.append(QString::number(logClusters[i]));
		}
		LOG(("text: %1 (from: %2, length: %3) part: %4").arg(DebugCurrentParsingString).arg(DebugCurrentParsingFrom).arg(DebugCurrentParsingLength).arg(DebugCurrentParsingPart));
		LOG(("pos: %1, end: %2, glyphPosition: %3, glyphCount: %4, lineLength: %5, num_glyphs: %6, logClusters[0..pos]: %7").arg(pos).arg(end).arg(glyphPosition).arg(glyphCount).arg(line.length).arg(current.num_glyphs).arg(str.join(",")));
		Unexpected("Values in addNextCluster()");
	}
	Q_ASSERT((pos == end && glyphPosition == current.num_glyphs)
		|| logClusters[pos] == glyphPosition);

	++glyphCount;
}

} // anonymous namespace

class BlockParser {
public:
	BlockParser(
		QTextEngine *e,
		TextBlock *b,
		QFixed minResizeWidth,
		int blockFrom,
		const QString &str);

private:
	void parseWords(QFixed minResizeWidth, int blockFrom);
	bool isLineBreak(const QCharAttributes *attributes, int index);
	bool isSpaceBreak(const QCharAttributes *attributes, int index);

	TextBlock *block;
	QTextEngine *eng;
	const QString &str;

};

BlockParser::BlockParser(
	QTextEngine *e,
	TextBlock *b,
	QFixed minResizeWidth,
	int blockFrom,
	const QString &str)
: block(b)
, eng(e)
, str(str) {
	parseWords(minResizeWidth, blockFrom);
}

void BlockParser::parseWords(QFixed minResizeWidth, int blockFrom) {
	LineBreakHelper lbh;

	// Helper for debugging crashes in text processing.
	//
	//		auto debugChars = QString();
	//		debugChars.reserve(str.size() * 7);
	//		for (const auto ch : str) {
	//			debugChars.append(
	//				"0x").append(
	//				QString::number(ch.unicode(), 16).toUpper()).append(
	//				' ');
	//		}
	//		LOG(("Text: %1, chars: %2").arg(str).arg(debugChars));

	int item = -1;
	int newItem = eng->findItem(0);

	const QCharAttributes *attributes = eng->attributes();
	if (!attributes)
		return;
	int end = 0;
	lbh.logClusters = eng->layoutData->logClustersPtr;

	block->_words.clear();

	int wordStart = lbh.currentPosition;

	bool addingEachGrapheme = false;
	int lastGraphemeBoundaryPosition = -1;
	ScriptLine lastGraphemeBoundaryLine;

	while (newItem < eng->layoutData->items.size()) {
		if (newItem != item) {
			item = newItem;
			const QScriptItem &current = eng->layoutData->items[item];
			if (!current.num_glyphs) {
				eng->shape(item);
				attributes = eng->attributes();
				if (!attributes)
					return;
				lbh.logClusters = eng->layoutData->logClustersPtr;
			}
			lbh.currentPosition = current.position;
			end = current.position + eng->length(item);
			lbh.glyphs = eng->shapedGlyphs(&current);
			QFontEngine *fontEngine = eng->fontEngine(current);
			if (lbh.fontEngine != fontEngine) {
				lbh.fontEngine = fontEngine;
			}
		}
		const QScriptItem &current = eng->layoutData->items[item];

		const auto atSpaceBreak = [&] {
			for (auto index = lbh.currentPosition; index < end; ++index) {
				if (!attributes[index].whiteSpace) {
					return false;
				} else if (isSpaceBreak(attributes, index)) {
					return true;
				}
			}
			return false;
		}();
		if (atSpaceBreak) {
			while (lbh.currentPosition < end && attributes[lbh.currentPosition].whiteSpace)
				addNextCluster(lbh.currentPosition, end, lbh.spaceData, lbh.glyphCount,
					current, lbh.logClusters, lbh.glyphs);

			if (block->_words.isEmpty()) {
				block->_words.push_back(TextWord(wordStart + blockFrom, lbh.tmpData.textWidth, -lbh.negativeRightBearing()));
			}
			block->_words.back().add_rpadding(lbh.spaceData.textWidth);
			block->_width += lbh.spaceData.textWidth;
			lbh.spaceData.length = 0;
			lbh.spaceData.textWidth = 0;

			wordStart = lbh.currentPosition;

			addingEachGrapheme = false;
			lastGraphemeBoundaryPosition = -1;
			lastGraphemeBoundaryLine = ScriptLine();
		} else {
			do {
				addNextCluster(lbh.currentPosition, end, lbh.tmpData, lbh.glyphCount,
					current, lbh.logClusters, lbh.glyphs);

				if (lbh.currentPosition >= eng->layoutData->string.length()
					|| isSpaceBreak(attributes, lbh.currentPosition)
					|| isLineBreak(attributes, lbh.currentPosition)) {
					lbh.calculateRightBearing();
					block->_words.push_back(TextWord(wordStart + blockFrom, lbh.tmpData.textWidth, -lbh.negativeRightBearing()));
					block->_width += lbh.tmpData.textWidth;
					lbh.tmpData.textWidth = 0;
					lbh.tmpData.length = 0;
					wordStart = lbh.currentPosition;
					break;
				} else if (attributes[lbh.currentPosition].graphemeBoundary) {
					if (!addingEachGrapheme && lbh.tmpData.textWidth > minResizeWidth) {
						if (lastGraphemeBoundaryPosition >= 0) {
							lbh.calculateRightBearingForPreviousGlyph();
							block->_words.push_back(TextWord(wordStart + blockFrom, -lastGraphemeBoundaryLine.textWidth, -lbh.negativeRightBearing()));
							block->_width += lastGraphemeBoundaryLine.textWidth;
							lbh.tmpData.textWidth -= lastGraphemeBoundaryLine.textWidth;
							lbh.tmpData.length -= lastGraphemeBoundaryLine.length;
							wordStart = lastGraphemeBoundaryPosition;
						}
						addingEachGrapheme = true;
					}
					if (addingEachGrapheme) {
						lbh.calculateRightBearing();
						block->_words.push_back(TextWord(wordStart + blockFrom, -lbh.tmpData.textWidth, -lbh.negativeRightBearing()));
						block->_width += lbh.tmpData.textWidth;
						lbh.tmpData.textWidth = 0;
						lbh.tmpData.length = 0;
						wordStart = lbh.currentPosition;
					} else {
						lastGraphemeBoundaryPosition = lbh.currentPosition;
						lastGraphemeBoundaryLine = lbh.tmpData;
						lbh.saveCurrentGlyph();
					}
				}
			} while (lbh.currentPosition < end);
		}
		if (lbh.currentPosition == end)
			newItem = item + 1;
	}
	if (!block->_words.isEmpty()) {
		block->_rpadding = block->_words.back().f_rpadding();
		block->_width -= block->_rpadding;
		block->_words.squeeze();
	}
}

bool BlockParser::isLineBreak(
		const QCharAttributes *attributes,
		int index) {
	// Don't break after / in links.
	return attributes[index].lineBreak
		&& (block->lnkIndex() <= 0
			|| index <= 0
			|| str[index - 1] != '/');
}

bool BlockParser::isSpaceBreak(
		const QCharAttributes *attributes,
		int index) {
	// Don't break on &nbsp;
	return attributes[index].whiteSpace
		&& (str[index] != QChar::Nbsp);
}

AbstractBlock::AbstractBlock(
	const style::font &font,
	const QString &str,
	uint16 from,
	uint16 length,
	uint16 flags,
	uint16 lnkIndex,
	uint16 spoilerIndex)
: _flags((flags & 0b1111111111) | ((lnkIndex & 0xFFFF) << 14))
, _from(from)
, _spoilerIndex(spoilerIndex) {
}

uint16 AbstractBlock::from() const {
	return _from;
}

int AbstractBlock::width() const {
	return _width.toInt();
}

int AbstractBlock::rpadding() const {
	return _rpadding.toInt();
}

QFixed AbstractBlock::f_width() const {
	return _width;
}

QFixed AbstractBlock::f_rpadding() const {
	return _rpadding;
}

uint16 AbstractBlock::lnkIndex() const {
	return (_flags >> 14) & 0xFFFF;
}

void AbstractBlock::setLnkIndex(uint16 lnkIndex) {
	_flags = (_flags & ~(0xFFFF << 14)) | (lnkIndex << 14);
}

uint16 AbstractBlock::spoilerIndex() const {
	return _spoilerIndex;
}

void AbstractBlock::setSpoilerIndex(uint16 spoilerIndex) {
	_spoilerIndex = spoilerIndex;
}

TextBlockType AbstractBlock::type() const {
	return TextBlockType((_flags >> 10) & 0x0F);
}

int32 AbstractBlock::flags() const {
	return (_flags & 0b1111111111);
}

QFixed AbstractBlock::f_rbearing() const {
	return (type() == TextBlockTText)
		? static_cast<const TextBlock*>(this)->real_f_rbearing()
		: 0;
}

TextBlock::TextBlock(
	const style::font &font,
	const QString &str,
	QFixed minResizeWidth,
	uint16 from,
	uint16 length,
	uint16 flags,
	uint16 lnkIndex,
	uint16 spoilerIndex)
: AbstractBlock(font, str, from, length, flags, lnkIndex, spoilerIndex) {
	_flags |= ((TextBlockTText & 0x0F) << 10);
	if (length) {
		style::font blockFont = font;
		if (!flags && lnkIndex) {
			// should use TextStyle lnkFlags somehow... not supported
		}

		if ((flags & TextBlockFPre) || (flags & TextBlockFCode)) {
			blockFont = blockFont->monospace();
		} else {
			if (flags & TextBlockFBold) {
				blockFont = blockFont->bold();
			} else if (flags & TextBlockFSemibold) {
				blockFont = blockFont->semibold();
			}
			if (flags & TextBlockFItalic) blockFont = blockFont->italic();
			if (flags & TextBlockFUnderline) blockFont = blockFont->underline();
			if (flags & TextBlockFStrikeOut) blockFont = blockFont->strikeout();
			if (flags & TextBlockFTilde) { // tilde fix in OpenSans
				blockFont = blockFont->semibold();
			}
		}

		DebugCurrentParsingString = str;
		DebugCurrentParsingFrom = _from;
		DebugCurrentParsingLength = length;
		const auto part = DebugCurrentParsingPart = str.mid(_from, length);

		QStackTextEngine engine(part, blockFont->f);
		BlockParser parser(&engine, this, minResizeWidth, _from, part);
	}
}

QFixed TextBlock::real_f_rbearing() const {
	return _words.isEmpty() ? 0 : _words.back().f_rbearing();
}

EmojiBlock::EmojiBlock(
	const style::font &font,
	const QString &str,
	uint16 from,
	uint16 length,
	uint16 flags,
	uint16 lnkIndex,
	uint16 spoilerIndex,
	EmojiPtr emoji)
: AbstractBlock(font, str, from, length, flags, lnkIndex, spoilerIndex)
, _emoji(emoji) {
	_flags |= ((TextBlockTEmoji & 0x0F) << 10);
	_width = int(st::emojiSize + 2 * st::emojiPadding);
	_rpadding = 0;
	for (auto i = length; i != 0;) {
		auto ch = str[_from + (--i)];
		if (ch.unicode() == QChar::Space) {
			_rpadding += font->spacew;
		} else {
			break;
		}
	}
}

CustomEmojiBlock::CustomEmojiBlock(
	const style::font &font,
	const QString &str,
	uint16 from,
	uint16 length,
	uint16 flags,
	uint16 lnkIndex,
	uint16 spoilerIndex,
	std::unique_ptr<CustomEmoji> custom)
: AbstractBlock(font, str, from, length, flags, lnkIndex, spoilerIndex)
, _custom(std::move(custom)) {
	_flags |= ((TextBlockTCustomEmoji & 0x0F) << 10);
	_width = int(st::emojiSize + 2 * st::emojiPadding);
	_rpadding = 0;
	for (auto i = length; i != 0;) {
		auto ch = str[_from + (--i)];
		if (ch.unicode() == QChar::Space) {
			_rpadding += font->spacew;
		} else {
			break;
		}
	}
}

NewlineBlock::NewlineBlock(
	const style::font &font,
	const QString &str,
	uint16 from,
	uint16 length,
	uint16 flags,
	uint16 lnkIndex,
	uint16 spoilerIndex)
: AbstractBlock(font, str, from, length, flags, lnkIndex, spoilerIndex) {
	_flags |= ((TextBlockTNewline & 0x0F) << 10);
}

Qt::LayoutDirection NewlineBlock::nextDirection() const {
	return _nextDir;
}

SkipBlock::SkipBlock(
	const style::font &font,
	const QString &str,
	uint16 from,
	int32 w,
	int32 h,
	uint16 lnkIndex,
	uint16 spoilerIndex)
: AbstractBlock(font, str, from, 1, 0, lnkIndex, spoilerIndex)
, _height(h) {
	_flags |= ((TextBlockTSkip & 0x0F) << 10);
	_width = w;
}

int SkipBlock::height() const {
	return _height;
}


TextWord::TextWord(
	uint16 from,
	QFixed width,
	QFixed rbearing,
	QFixed rpadding)
: _from(from)
, _rbearing((rbearing.value() > 0x7FFF)
	? 0x7FFF
	: (rbearing.value() < -0x7FFF ? -0x7FFF : rbearing.value()))
, _width(width)
, _rpadding(rpadding) {
}

uint16 TextWord::from() const {
	return _from;
}

QFixed TextWord::f_rbearing() const {
	return QFixed::fromFixed(_rbearing);
}

QFixed TextWord::f_width() const {
	return _width;
}

QFixed TextWord::f_rpadding() const {
	return _rpadding;
}

void TextWord::add_rpadding(QFixed padding) {
	_rpadding += padding;
}

Block::Block() {
	Unexpected("Should not be called.");
}

Block::Block(Block &&other) {
	switch (other->type()) {
	case TextBlockTNewline:
		emplace<NewlineBlock>(std::move(other.unsafe<NewlineBlock>()));
		break;
	case TextBlockTText:
		emplace<TextBlock>(std::move(other.unsafe<TextBlock>()));
		break;
	case TextBlockTEmoji:
		emplace<EmojiBlock>(std::move(other.unsafe<EmojiBlock>()));
		break;
	case TextBlockTCustomEmoji:
		emplace<CustomEmojiBlock>(std::move(other.unsafe<CustomEmojiBlock>()));
		break;
	case TextBlockTSkip:
		emplace<SkipBlock>(std::move(other.unsafe<SkipBlock>()));
		break;
	default:
		Unexpected("Bad text block type in Block(Block&&).");
	}
}

Block &Block::operator=(Block &&other) {
	if (&other == this) {
		return *this;
	}
	destroy();
	switch (other->type()) {
	case TextBlockTNewline:
		emplace<NewlineBlock>(std::move(other.unsafe<NewlineBlock>()));
		break;
	case TextBlockTText:
		emplace<TextBlock>(std::move(other.unsafe<TextBlock>()));
		break;
	case TextBlockTEmoji:
		emplace<EmojiBlock>(std::move(other.unsafe<EmojiBlock>()));
		break;
	case TextBlockTCustomEmoji:
		emplace<CustomEmojiBlock>(std::move(other.unsafe<CustomEmojiBlock>()));
		break;
	case TextBlockTSkip:
		emplace<SkipBlock>(std::move(other.unsafe<SkipBlock>()));
		break;
	default:
		Unexpected("Bad text block type in operator=(Block&&).");
	}
	return *this;
}

Block::~Block() {
	destroy();
}

Block Block::Newline(
		const style::font &font,
		const QString &str,
		uint16 from,
		uint16 length,
		uint16 flags,
		uint16 lnkIndex,
		uint16 spoilerIndex) {
	return New<NewlineBlock>(
		font,
		str,
		from,
		length,
		flags,
		lnkIndex,
		spoilerIndex);
}

Block Block::Text(
		const style::font &font,
		const QString &str,
		QFixed minResizeWidth,
		uint16 from,
		uint16 length,
		uint16 flags,
		uint16 lnkIndex,
		uint16 spoilerIndex) {
	return New<TextBlock>(
		font,
		str,
		minResizeWidth,
		from,
		length,
		flags,
		lnkIndex,
		spoilerIndex);
}

Block Block::Emoji(
		const style::font &font,
		const QString &str,
		uint16 from,
		uint16 length,
		uint16 flags,
		uint16 lnkIndex,
		uint16 spoilerIndex,
		EmojiPtr emoji) {
	return New<EmojiBlock>(
		font,
		str,
		from,
		length,
		flags,
		lnkIndex,
		spoilerIndex,
		emoji);
}

Block Block::CustomEmoji(
		const style::font &font,
		const QString &str,
		uint16 from,
		uint16 length,
		uint16 flags,
		uint16 lnkIndex,
		uint16 spoilerIndex,
		std::unique_ptr<Text::CustomEmoji> custom) {
	return New<CustomEmojiBlock>(
		font,
		str,
		from,
		length,
		flags,
		lnkIndex,
		spoilerIndex,
		std::move(custom));
}

Block Block::Skip(
		const style::font &font,
		const QString &str,
		uint16 from,
		int32 w,
		int32 h,
		uint16 lnkIndex,
		uint16 spoilerIndex) {
	return New<SkipBlock>(font, str, from, w, h, lnkIndex, spoilerIndex);
}

AbstractBlock *Block::get() {
	return &unsafe<AbstractBlock>();
}

const AbstractBlock *Block::get() const {
	return &unsafe<AbstractBlock>();
}

AbstractBlock *Block::operator->() {
	return get();
}

const AbstractBlock *Block::operator->() const {
	return get();
}

AbstractBlock &Block::operator*() {
	return *get();
}

const AbstractBlock &Block::operator*() const {
	return *get();
}

void Block::destroy() {
	switch (get()->type()) {
	case TextBlockTNewline:
		unsafe<NewlineBlock>().~NewlineBlock();
		break;
	case TextBlockTText:
		unsafe<TextBlock>().~TextBlock();
		break;
	case TextBlockTEmoji:
		unsafe<EmojiBlock>().~EmojiBlock();
		break;
	case TextBlockTCustomEmoji:
		unsafe<CustomEmojiBlock>().~CustomEmojiBlock();
		break;
	case TextBlockTSkip:
		unsafe<SkipBlock>().~SkipBlock();
		break;
	default:
		Unexpected("Bad text block type in Block(Block&&).");
	}
}

int CountBlockHeight(
		const AbstractBlock *block,
		const style::TextStyle *st) {
	return (block->type() == TextBlockTSkip)
		? static_cast<const SkipBlock*>(block)->height()
		: (st->lineHeight > st->font->height)
		? st->lineHeight
		: st->font->height;
}

} // namespace Text
} // namespace Ui
