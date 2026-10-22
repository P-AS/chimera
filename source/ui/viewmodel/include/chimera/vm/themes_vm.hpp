#pragma once
/* Themes: Chimera's colour themes - its built-in Light and Dark, and every
 * .chimeraTheme.json in the themes folder - one chosen and remembered. A
 * theme is named colours ("WindowBackground", "TasGreenZone"...); the window
 * turns the chosen one's into its palette and the piano roll's colours. */

#include <string>
#include <vector>

#include "chimera/vm/json.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class ThemesViewModel : public ViewModel
{
public:
	explicit ThemesViewModel(Dispatcher &dispatcher);

	/* a theme the program carries (its text, as a theme file is) */
	bool addBuiltIn(const std::string &text, std::string &error);

	/* the folder themes are read from */
	Property<std::string> folder;
	/* the theme chosen, by name (Chimera's default is Dark) */
	Property<std::string> current{"Dark"};
	/* the chosen theme's colours, {"Role": "#RRGGBB"}, and whether it is dark */
	Property<std::string> colors{"{}"};
	Property<bool> dark{true};
	Property<int64_t> count{0};
	Property<std::string> status;

private:
	struct Theme
	{
		std::string name, author, description, origin;
		bool dark = false;
		json::Json colors;
	};
	class Rows : public RowModel
	{
	public:
		explicit Rows(ThemesViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.themes_.size()); }
		int32_t columnCount() const override { return 9; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		ThemesViewModel &vm_;
	};

	static bool read(const std::string &text, const std::string &origin, Theme &out, std::string &error);
	void refresh();
	void show();

	std::vector<Theme> builtIn_, themes_;
	Rows rows_{*this};
	Connection folderWatch_, currentWatch_;
};

} // namespace chimera::vm
