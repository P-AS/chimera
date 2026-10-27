#pragma once
/* Lua's forms library, as Chimera has it: a script's own small windows -
 * forms holding buttons, labels, text boxes, check boxes, dropdowns and
 * picture boxes it draws on - kept here as rows the window draws, each with
 * a handle the script names it by. A click and a closed form go back to the
 * script (the window's signals). */

#include <functional>
#include <string>
#include <vector>

#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class FormsViewModel : public ViewModel
{
public:
	explicit FormsViewModel(Dispatcher &dispatcher);

	/* forms.openfile: the window's file dialog, shown and waited for (modal,
	 * as Chimera's): the file named at first, the folder it opens in, the
	 * filters (nameFilters' form); the file picked, "" when cancelled */
	std::function<std::string(const std::string &fileName, const std::string &folder, const std::vector<std::string> &filters)> openFile;
	/* a .NET dialog filter ("Images (*.bmp;*.png)|*.bmp;*.png|All Files (*.*)|*.*")
	 * as a file dialog's name filters ("Images (*.bmp *.png)", "All Files (*)") */
	static std::vector<std::string> nameFilters(const std::string &filter);

	struct Item
	{
		int64_t handle = 0, form = 0;
		/* form, button, label, textbox, checkbox, dropdown, picturebox */
		std::string kind, text;
		int64_t x = 0, y = 0, width = 0, height = 0;
		bool checked = false, multiline = false, fixedWidth = false, visible = true, enabled = true;
		std::vector<std::string> items;
		int64_t selected = -1;
		std::string boxType, scrollBars;
		/* a picture box: what is drawn on it (the gui library's shapes, JSON) and its colour behind them */
		std::string shapes = "[]", background = "#FFFFFFFF";
		int64_t mouseX = 0, mouseY = 0;
		/* a picture box asked to be saved as a picture: the file, and a count
		 * that goes up with each ask (the window saves what it drew) */
		std::string saveTo;
		int64_t saves = 0;
	};

	/* a button clicked (any control the script gave a click to), a form closed */
	Signal<int64_t> clicked, closed;

private:
	class Rows : public RowModel
	{
	public:
		Rows(FormsViewModel &vm, bool forms) : vm_(vm), forms_(forms) {}
		int64_t rowCount() const override;
		int32_t columnCount() const override { return 21; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		const Item &at(int64_t row) const;
		FormsViewModel &vm_;
		bool forms_;
	};
	Item *find(int64_t handle);
	int64_t add(Item item, std::string &error);
	void changed(const Item &item);
	void removeForm(int64_t handle);
	std::vector<Item> forms_, controls_;
	int64_t next_ = 1000;
	Rows formRows_{*this, true}, controlRows_{*this, false};
};

} // namespace chimera::vm
