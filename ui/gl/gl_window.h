// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once


namespace Ui {
class RpWindow;
class RpWidget;
} // namespace Ui

namespace Ui::GL {

enum class Backend;
struct Capabilities;

class Window final {
public:
	Window();
	explicit Window(Fn<Backend(Capabilities)> chooseBackend);
	~Window();

	[[nodiscard]] Backend backend() const;
	[[nodiscard]] not_null<RpWindow*> window() const;
	[[nodiscard]] not_null<RpWidget*> widget() const;

private:
	[[nodiscard]] std::unique_ptr<RpWindow> createWindow(
		const Fn<Backend(Capabilities)> &chooseBackend);
	[[nodiscard]] std::unique_ptr<RpWidget> createNativeBodyWrap(
		const Fn<Backend(Capabilities)> &chooseBackend);

	Backend _backend = Backend();
	const std::unique_ptr<RpWindow> _window;
	const std::unique_ptr<RpWidget> _bodyNativeWrap;
	const not_null<RpWidget*> _body;

};

} // namespace Ui::GL
