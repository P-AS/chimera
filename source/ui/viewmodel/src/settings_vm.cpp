#include "chimera/vm/settings_vm.hpp"

#include "chimera/engine.h"

namespace chimera::vm {

using json::Json;

SettingsViewModel::SettingsViewModel(Dispatcher &dispatcher, SessionViewModel &session)
	: ViewModel("settings", dispatcher), session_(session)
{
	applied_.kind = Json::Kind::Object;
	form_.setApplied(&applied_);
	expose("pending", "edits not applied yet", pending);
	expose("shown", "settings that apply to this machine and game", shown);
	expose("status", "what the settings last had to say", status);
	rows("settings", "one row a setting: name, label, type, value, options, option labels, min, max, description, edited", rows_);

	command("set", "changes a setting (applied by apply)", {"name", "value"},
		[this](const Args &a, std::string &error) -> Value {
			std::string name;
			if (!arg(a, 0, name, error)) return {};
			if (a.size() < 2)
			{
				error = "argument 2 is missing";
				return {};
			}
			if (!form_.set(name, a[1], error)) return {};
			pending.set(json::write(form_.values()) != json::write(applied_));
			evaluate();
			return {};
		}, &session_.isOpen);
	command("discard", "forgets the edits not applied", {}, [this](const Args &, std::string &) -> Value {
		form_.setValues(applied_);
		pending.set(false);
		evaluate();
		return {};
	}, &pending);
	command("apply", "gives the edits to the machine: live when the core can take them, else it starts again with them",
		{}, [this](const Args &, std::string &) -> Value {
			const Json values = form_.values();
			status.set("Applying...");
			session_.changeSettings(json::write(values), [this, values](bool ok, std::string what) {
				if (ok)
				{
					applied_ = values;
					pending.set(json::write(form_.values()) != json::write(applied_));
				}
				status.set(what);
				evaluate();
			});
			return {};
		}, &pending);

	/* each machine opened: its declaration, and the values it was opened with */
	opensWatch_ = session_.opens.changed.connect([this] {
		session_.onMachine([this](ce_session *s) {
			if (s == nullptr) return;
			std::string config;
			const char *perr = nullptr;
			if (ce_package *p = ce_package_open(session_.packageOnMachine().c_str(), &perr))
			{
				uint64_t len = 0;
				if (const uint8_t *d = ce_package_entry(p, "waterbox.config", &len))
					config.assign(reinterpret_cast<const char *>(d), static_cast<size_t>(len));
				ce_package_free(p);
			}
			const char *game = ce_session_game_settings(s);
			const std::string gameText = game != nullptr ? game : "";
			const std::string values = session_.settingsOnMachine(), slots = session_.slotsOnMachine();
			later([this, config, gameText, values, slots] {
				std::vector<Json> decl;
				std::string error, machineSetting;
				Json cfg, extra, v;
				if (json::parse(config, cfg, error))
				{
					if (const Json *list = cfg.find("settings")) decl = list->items;
					if (const Json *m = cfg.find("machineSetting"); m != nullptr && m->kind == Json::Kind::String)
						machineSetting = m->text;
				}
				if (!gameText.empty() && json::parse(gameText, extra, error))
					for (const Json &d : extra.items) decl.push_back(d);
				const size_t declared = decl.size();
				form_.declare(std::move(decl), machineSetting);
				form_.setSlots(slots);
				if (!json::parse(values, v, error) || v.kind != Json::Kind::Object) v = Json(), v.kind = Json::Kind::Object;
				applied_ = v;
				form_.setValues(v);
				pending.set(false);
				status.set(std::to_string(declared) + " settings declared");
				evaluate();
			});
		});
	});
}

void SettingsViewModel::evaluate()
{
	form_.evaluate();
	shown.set(static_cast<int64_t>(form_.exposed().size()));
	rows_.reset.emit();
}

} // namespace chimera::vm
