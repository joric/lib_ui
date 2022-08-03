// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/text/text_entity.h"
#include "ui/text/text_block.h"
#include "ui/painter.h"
#include "ui/click_handler.h"
#include "base/flags.h"

#include <private/qfixed_p.h>
#include <any>

class SpoilerClickHandler;

namespace Ui {
static const auto kQEllipsis = QStringLiteral("...");
} // namespace Ui

static const QChar TextCommand(0x0010);

struct TextParseOptions {
	int32 flags;
	int32 maxw;
	int32 maxh;
	Qt::LayoutDirection dir;
};
extern const TextParseOptions kDefaultTextOptions;
extern const TextParseOptions kMarkupTextOptions;
extern const TextParseOptions kPlainTextOptions;

enum class TextSelectType {
	Letters    = 0x01,
	Words      = 0x02,
	Paragraphs = 0x03,
};

struct TextSelection {
	constexpr TextSelection() = default;
	constexpr TextSelection(uint16 from, uint16 to) : from(from), to(to) {
	}
	constexpr bool empty() const {
		return from == to;
	}
	uint16 from = 0;
	uint16 to = 0;
};

inline bool operator==(TextSelection a, TextSelection b) {
	return a.from == b.from && a.to == b.to;
}

inline bool operator!=(TextSelection a, TextSelection b) {
	return !(a == b);
}

static constexpr TextSelection AllTextSelection = { 0, 0xFFFF };

namespace Ui::Text {

struct IsolatedEmoji;
struct OnlyCustomEmoji;

struct StateRequest {
	enum class Flag {
		BreakEverywhere = (1 << 0),
		LookupSymbol = (1 << 1),
		LookupLink = (1 << 2),
		LookupCustomTooltip = (1 << 3),
	};
	using Flags = base::flags<Flag>;
	friend inline constexpr auto is_flag_type(Flag) { return true; };

	StateRequest() {
	}

	style::align align = style::al_left;
	Flags flags = Flag::LookupLink;
};

struct StateResult {
	ClickHandlerPtr link;
	bool uponSymbol = false;
	bool afterSymbol = false;
	uint16 symbol = 0;
};

struct StateRequestElided : public StateRequest {
	StateRequestElided() {
	}
	StateRequestElided(const StateRequest &other) : StateRequest(other) {
	}
	int lines = 1;
	int removeFromEnd = 0;
};

class String {
public:
	String(int32 minResizeWidth = QFIXED_MAX);
	String(
		const style::TextStyle &st,
		const QString &text,
		const TextParseOptions &options = kDefaultTextOptions,
		int32 minResizeWidth = QFIXED_MAX);
	String(String &&other) = default;
	String &operator=(String &&other) = default;
	~String() = default;

	[[nodiscard]] int countWidth(int width, bool breakEverywhere = false) const;
	[[nodiscard]] int countHeight(int width, bool breakEverywhere = false) const;
	void countLineWidths(int width, QVector<int> *lineWidths, bool breakEverywhere = false) const;
	void setText(const style::TextStyle &st, const QString &text, const TextParseOptions &options = kDefaultTextOptions);
	void setMarkedText(const style::TextStyle &st, const TextWithEntities &textWithEntities, const TextParseOptions &options = kMarkupTextOptions, const std::any &context = {});

	void setLink(uint16 lnkIndex, const ClickHandlerPtr &lnk);
	[[nodiscard]] bool hasLinks() const;
	void setSpoiler(
		uint16 lnkIndex,
		const std::shared_ptr<SpoilerClickHandler> &lnk);
	void setSpoilerShown(uint16 lnkIndex, bool shown);
	[[nodiscard]] int spoilersCount() const;

	[[nodiscard]] bool hasSkipBlock() const;
	bool updateSkipBlock(int width, int height);
	bool removeSkipBlock();

	[[nodiscard]] int maxWidth() const {
		return _maxWidth.ceil().toInt();
	}
	[[nodiscard]] int minHeight() const {
		return _minHeight;
	}
	[[nodiscard]] int countMaxMonospaceWidth() const;

	void draw(Painter &p, int32 left, int32 top, int32 width, style::align align = style::al_left, int32 yFrom = 0, int32 yTo = -1, TextSelection selection = { 0, 0 }, bool fullWidthSelection = true) const;
	void drawElided(Painter &p, int32 left, int32 top, int32 width, int32 lines = 1, style::align align = style::al_left, int32 yFrom = 0, int32 yTo = -1, int32 removeFromEnd = 0, bool breakEverywhere = false, TextSelection selection = { 0, 0 }) const;
	void drawLeft(Painter &p, int32 left, int32 top, int32 width, int32 outerw, style::align align = style::al_left, int32 yFrom = 0, int32 yTo = -1, TextSelection selection = { 0, 0 }) const;
	void drawLeftElided(Painter &p, int32 left, int32 top, int32 width, int32 outerw, int32 lines = 1, style::align align = style::al_left, int32 yFrom = 0, int32 yTo = -1, int32 removeFromEnd = 0, bool breakEverywhere = false, TextSelection selection = { 0, 0 }) const;
	void drawRight(Painter &p, int32 right, int32 top, int32 width, int32 outerw, style::align align = style::al_left, int32 yFrom = 0, int32 yTo = -1, TextSelection selection = { 0, 0 }) const;
	void drawRightElided(Painter &p, int32 right, int32 top, int32 width, int32 outerw, int32 lines = 1, style::align align = style::al_left, int32 yFrom = 0, int32 yTo = -1, int32 removeFromEnd = 0, bool breakEverywhere = false, TextSelection selection = { 0, 0 }) const;

	[[nodiscard]] StateResult getState(QPoint point, int width, StateRequest request = StateRequest()) const;
	[[nodiscard]] StateResult getStateLeft(QPoint point, int width, int outerw, StateRequest request = StateRequest()) const;
	[[nodiscard]] StateResult getStateElided(QPoint point, int width, StateRequestElided request = StateRequestElided()) const;
	[[nodiscard]] StateResult getStateElidedLeft(QPoint point, int width, int outerw, StateRequestElided request = StateRequestElided()) const;

	[[nodiscard]] TextSelection adjustSelection(TextSelection selection, TextSelectType selectType) const;
	[[nodiscard]] bool isFullSelection(TextSelection selection) const {
		return (selection.from == 0) && (selection.to >= _text.size());
	}

	[[nodiscard]] bool isEmpty() const;
	[[nodiscard]] bool isNull() const {
		return !_st;
	}
	[[nodiscard]] int length() const {
		return _text.size();
	}

	[[nodiscard]] QString toString(
		TextSelection selection = AllTextSelection) const;
	[[nodiscard]] TextWithEntities toTextWithEntities(
		TextSelection selection = AllTextSelection) const;
	[[nodiscard]] TextForMimeData toTextForMimeData(
		TextSelection selection = AllTextSelection) const;

	[[nodiscard]] bool hasCustomEmoji() const;
	void unloadCustomEmoji();

	[[nodiscard]] bool isIsolatedEmoji() const;
	[[nodiscard]] IsolatedEmoji toIsolatedEmoji() const;

	[[nodiscard]] bool isOnlyCustomEmoji() const;
	[[nodiscard]] OnlyCustomEmoji toOnlyCustomEmoji() const;

	[[nodiscard]] const style::TextStyle *style() const {
		return _st;
	}

	void clear();

private:
	using TextBlocks = std::vector<Block>;
	using TextLinks = QVector<ClickHandlerPtr>;

	uint16 countBlockEnd(const TextBlocks::const_iterator &i, const TextBlocks::const_iterator &e) const;
	uint16 countBlockLength(const TextBlocks::const_iterator &i, const TextBlocks::const_iterator &e) const;

	// Template method for originalText(), originalTextWithEntities().
	template <typename AppendPartCallback, typename ClickHandlerStartCallback, typename ClickHandlerFinishCallback, typename FlagsChangeCallback>
	void enumerateText(TextSelection selection, AppendPartCallback appendPartCallback, ClickHandlerStartCallback clickHandlerStartCallback, ClickHandlerFinishCallback clickHandlerFinishCallback, FlagsChangeCallback flagsChangeCallback) const;

	// Template method for countWidth(), countHeight(), countLineWidths().
	// callback(lineWidth, lineHeight) will be called for all lines with:
	// QFixed lineWidth, int lineHeight
	template <typename Callback>
	void enumerateLines(int w, bool breakEverywhere, Callback callback) const;

	void recountNaturalSize(bool initial, Qt::LayoutDirection optionsDir = Qt::LayoutDirectionAuto);

	// clear() deletes all blocks and calls this method
	// it is also called from move constructor / assignment operator
	void clearFields();

	[[nodiscard]] ClickHandlerPtr spoilerLink(uint16 spoilerIndex) const;

	TextForMimeData toText(
		TextSelection selection,
		bool composeExpanded,
		bool composeEntities) const;

	QFixed _minResizeWidth;
	QFixed _maxWidth = 0;
	int32 _minHeight = 0;
	bool _hasCustomEmoji : 1;
	bool _isIsolatedEmoji : 1;
	bool _isOnlyCustomEmoji : 1;

	QString _text;
	const style::TextStyle *_st = nullptr;

	TextBlocks _blocks;
	TextLinks _links;

	QVector<std::shared_ptr<SpoilerClickHandler>> _spoilers;

	Qt::LayoutDirection _startDir = Qt::LayoutDirectionAuto;

	struct {
		std::array<QImage, 4> corners;
		QColor color;
	} _spoilerCache, _spoilerShownCache;

	friend class Parser;
	friend class Renderer;

};

[[nodiscard]] bool IsWordSeparator(QChar ch);
[[nodiscard]] bool IsAlmostLinkEnd(QChar ch);
[[nodiscard]] bool IsLinkEnd(QChar ch);
[[nodiscard]] bool IsNewline(QChar ch);
[[nodiscard]] bool IsSpace(QChar ch);
[[nodiscard]] bool IsDiac(QChar ch);
[[nodiscard]] bool IsReplacedBySpace(QChar ch);
[[nodiscard]] bool IsTrimmed(QChar ch);

} // namespace Ui::Text

inline TextSelection snapSelection(int from, int to) {
	return { static_cast<uint16>(std::clamp(from, 0, 0xFFFF)), static_cast<uint16>(std::clamp(to, 0, 0xFFFF)) };
}
inline TextSelection shiftSelection(TextSelection selection, uint16 byLength) {
	return snapSelection(int(selection.from) + byLength, int(selection.to) + byLength);
}
inline TextSelection unshiftSelection(TextSelection selection, uint16 byLength) {
	return snapSelection(int(selection.from) - int(byLength), int(selection.to) - int(byLength));
}
inline TextSelection shiftSelection(TextSelection selection, const Ui::Text::String &byText) {
	return shiftSelection(selection, byText.length());
}
inline TextSelection unshiftSelection(TextSelection selection, const Ui::Text::String &byText) {
	return unshiftSelection(selection, byText.length());
}
