#ifndef slic3r_GUI_ShellThumbnail_hpp_
#define slic3r_GUI_ShellThumbnail_hpp_

#include <string>

namespace Slic3r { namespace GUI {

// Returns the preview the file explorer shows for `path`, encoded as PNG, or an empty
// string when the platform or the file type does not provide one.
// On Windows this goes through IShellItemImageFactory, which reuses whatever thumbnail
// handler the user already has registered for the extension (STL-Thumb for *.stl, F3D
// for *.step, ...) instead of rendering the model ourselves. Formats without a handler
// yield nothing rather than the generic file-type icon.
// `cached_only` restricts the lookup to the thumbnails Windows has already cached, which
// answers in a few milliseconds; generating a missing one spins up the shell's extractor
// host and can take about a second, so only call with false off the main thread.
std::string get_shell_thumbnail_png(const std::wstring &path, int size, bool cached_only);

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_ShellThumbnail_hpp_
