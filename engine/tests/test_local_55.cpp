// OpenBFME. GPL-3.0.
//
// Local 55 (fork issue #411, P-93 in docs/ennorath/PROBLEMS.md): in the skirmish menu's Rules tab (MpGameSetup.apt), the toggle buttons for
// Allow Custom Heroes (RuleCheckBox_0) and Allow Ring Heroes (RuleCheckBox_1) are CheckBox gadgets (window\apt\checkbox.wnd, STYLE =
// CHECKBOX+MOUSETRACK, input callback GadgetCheckBoxInput).
// RotWK GadgetCheckBoxInput (RW 0x0072937E .. 0x00729533; BFME2 0x00727938, Code/GameEngine/Source/GameClient/GUI/Gadget/GadgetCheckBox.cpp) calls
// PlaySound (RW 0x00815772, 2D UI event VIEW_ANY):
//   * GWM_MOUSE_ENTERING (RW 0x7294F4, when GWS_MOUSE_TRACK): "Gui_ShellMapMouseOver" (RW 0x729522 -> 0x815772);
//   * GWM_MOUSE_LEAVING  (RW 0x7294BF, when GWS_MOUSE_TRACK): "Gui_ShellMapMouseOut"  (RW 0x7294ED -> 0x729527 -> 0x815772);
//   * GWM_LEFT_UP        (RW 0x7293C9, when WIN_STATE_HILITED): "Gui_ShellMapSelect" (RW 0x7293FA -> 0x729527 -> 0x815772);
//   * GWM_LEFT_DOWN, GWM_RIGHT_UP, GWM_CHAR (KEY_SPACE / KEY_ENTER): no PlaySound.
#include "doctest.h"
#include "RetailTestMount.h"

#include "Common/ModMount.h"
#include "Common/RetailArchivePolicy.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include <cstdlib>
#include <filesystem>
#include "IniTestUtil.h"

#include "Common/Audio/AudioEntryPoints.h"
#include "Common/Audio/GameAudio.h"
#include "Common/Audio/SimulatedAudioDevice.h"
#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/AptScreen.h"
#include "GameClient/GUI/AptScreens/AptScreenFactories.h"
#include "GameClient/GUI/AptScreens/AptSkirmish.h"
#include "GameClient/GUI/Gadget.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/GameWindow.h"
#include "GameClient/GUI/GameWindowManager.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/GUI/WindowManager.h"
#include "Libraries/Source/Apt/AptButtonInst.h"
#include "Libraries/Source/Apt/AptCharacterInst.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <vector>

namespace
{
std::string wav(double ms)
{
	const std::uint32_t rate = 22050, frames = (std::uint32_t)(ms * rate / 1000.0);
	std::string out;
	auto u32 = [&](std::uint32_t v) {
		for (int i = 0; i < 4; ++i)
		{
			out.push_back((char)(v >> (8 * i)));
		}
	};
	auto u16 = [&](std::uint16_t v) {
		out.push_back((char)v);
		out.push_back((char)(v >> 8));
	};
	out += "RIFF";
	u32(36 + frames * 2);
	out += "WAVEfmt ";
	u32(16);
	u16(1);
	u16(1);
	u32(rate);
	u32(rate * 2);
	u16(2);
	u16(16);
	out += "data";
	u32(frames * 2);
	out.append(frames * 2, '\0');
	return out;
}

const char kCheckBoxAudio[] =
	"AudioSettings\n  AudioRoot = Data\\Audio\n  SoundsFolder = Sounds\n  MusicFolder = Tracks\n  StreamingFolder = Speech\n  AmbientStreamFolder = AmbientStreams\n  SoundsExtension = wav\n"
	"  SampleCount2D = 8\n  SampleCount3D = 4\n  StreamCount = 3\n  MinSampleVolume = 2%\n  DefaultSoundVolume = 100%\n  DefaultVoiceVolume = 100%\n  DefaultMusicVolume = 100%\n  DefaultAmbientVolume = 100%\n  DefaultMovieVolume = 100%\nEnd\n"
	"AudioEvent DefaultSoundEffect\n  Volume = 100\n  Priority = high\n  Limit = 0\n  MinRange = 160\n  MaxRange = 640\n  PlayPercent = 100\n  Type = ui everyone\n  Type = +DEFAULT\nEnd\n"
	"AudioEvent Gui_ShellMapSelect\n  Sounds = cb_select\n  Limit = 0\n  Type = ui player\nEnd\n"
	"AudioEvent Gui_ShellMapMouseOver\n  Sounds = cb_over\n  Limit = 0\n  Type = ui player\nEnd\n"
	"AudioEvent Gui_ShellMapMouseOut\n  Sounds = cb_out\n  Limit = 0\n  Type = ui player\nEnd\n";

struct SynthAudioRig
{
	initest::Fixture fx;
	AudioIniState ini;
	std::unique_ptr<AudioAssetCache> cache;
	std::unique_ptr<SimulatedAudioDevice> device;
	std::unique_ptr<AudioManager> audio;
	double t = 0.0;
	SynthAudioRig()
	{
		fx.mount({
			{ "data\\audio\\sounds\\cb_select.wav", wav(10) },
			{ "data\\audio\\sounds\\cb_over.wav", wav(10) },
			{ "data\\audio\\sounds\\cb_out.wav", wav(10) },
		});
		ini.registerBlocks(fx.env.blocks);
		const std::string err = initest::loadError(fx.env, "audio.ini", kCheckBoxAudio);
		REQUIRE_MESSAGE(err.empty(), err);
		cache = std::make_unique<AudioAssetCache>(&fx.fsys, 8u << 20);
		device = std::make_unique<SimulatedAudioDevice>(cache.get());
		audio = std::make_unique<AudioManager>(ini, *cache, *device, RandomAlgorithm::RotWK_GameDat_LCG, 1);
		audio->enableEventLog(true);
		audio->update(0.0);
		AudioApi::install(audio.get());
	}
	~SynthAudioRig() { AudioApi::install(nullptr); }
	std::vector<std::string> takeRequestedEvents()
	{
		t += 200.0;
		device->setTime(t);
		audio->update(t);
		std::vector<std::string> out;
		for (const AudioLogEntry &e : audio->takeEventLog())
		{
			if (!e.play)
			{
				out.push_back(e.event);
			}
		}
		return out;
	}
	int countFileStarts(const std::string &needle)
	{
		int n = 0;
		for (const auto &e : device->log())
		{
			if (e.kind == SimulatedAudioDevice::LogEntry::Start && e.file.find(needle) != std::string::npos)
			{
				++n;
			}
		}
		return n;
	}
};

std::u16string toU16(const std::string &s)
{
	return std::u16string(s.begin(), s.end());
}

class RulesLabelsText : public GameTextSource
{
public:
	std::map<std::string, std::u16string> labels;
	bool fetch(const std::string &label, std::u16string &out) const override
	{
		auto it = labels.find(label);
		if (it == labels.end())
		{
			return false;
		}
		out = it->second;
		return true;
	}
	bool fetchMapLabel(const std::string &, const std::string &, std::u16string &) const override { return false; }
	void set(const std::string &label, const std::string &text) { labels[label] = toU16(text); }
};

void fillLobbyLabels(RulesLabelsText &t)
{
	t.set("GUI:Open", "Open");
	t.set("GUI:Closed", "Closed");
	t.set("GUI:EasyAI", "Easy AI");
	t.set("GUI:MediumAI", "Medium AI");
	t.set("GUI:HardAI", "Hard AI");
	t.set("GUI:BrutalAI", "Brutal AI");
	t.set("GUI:Random", "Random");
	t.set("GUI:???", "???");
	t.set("GUI:Player", "Player");
	t.set("GUI:StartingMoneyFormat", "$%d");
	t.set("GUI:ErrorStartingGame", "Error starting game");
	t.set("GUI:CantFindMap", "Cannot find the map");
	t.set("LAN:TooManyPlayers", "Too many players: the map has %d");
	for (int i = 0; i <= 4; ++i)
	{
		t.set("Team:" + std::to_string(i), "Team " + std::to_string(i));
	}
	for (const char *c : { "Blue", "Red", "Gold", "Green", "Orange", "Purple", "SkyBlue", "Pink", "Gray", "White" })
	{
		t.set(std::string("Color:") + c, c);
	}
	t.set("RULE:InitialResources", "Initial Resources");
	t.set("RULE:CommandPointFactor", "Command Point Factor");
	t.set("RULE:AllowCustomHeroes", "Allow Custom Heroes");
	t.set("RULE:AllowRingHeroes", "Allow Ring Heroes");
	t.set("VALUE:MoneyFormat", "$%d");
	t.set("VALUE:Default", " (Default)");
	t.set("VALUE:1X", "1X");
	t.set("VALUE:2X", "2X");
	t.set("APT:CommandPointsTooHigh", "Warning: too slow");
}

AptButtonInst *firstButtonIn(AptCharacterInst *c)
{
	if (!c)
	{
		return nullptr;
	}
	if (AptButtonInst *b = c->asButton())
	{
		return b;
	}
	if (AptSpriteInst *sp = c->asSprite())
	{
		for (AptCharacterInst *k : sp->children())
		{
			if (AptButtonInst *b = firstButtonIn(k))
			{
				return b;
			}
		}
	}
	return nullptr;
}

std::string lowerAscii(std::string s)
{
	for (char &ch : s)
	{
		ch = (char)std::tolower((unsigned char)ch);
	}
	return s;
}

struct EnnorathMount
{
	Win32BIGFileSystem pure;
	Win32BIGFileSystem merged;
	bool loaded = false;
	bool load(const char *what)
	{
		if (loaded)
		{
			return true;
		}
		auto env = [](const char *k) -> std::string {
			const char *v = std::getenv(k);
			return v ? v : "";
		};
		const std::string rotwk = env("ROTWK_INSTALL"), bfme2 = env("BFME2_INSTALL"), mod = env("OPENBFME_ENNORATH_INSTALL");
		if (rotwk.empty() || bfme2.empty() || mod.empty())
		{
			retailtest::printSkip((std::string(what) + " (set ROTWK_INSTALL, BFME2_INSTALL, OPENBFME_ENNORATH_INSTALL)").c_str());
			return false;
		}
		std::string err;
		std::vector<RetailInstall> installs(2);
		installs[0].label = "rotwk";
		installs[0].root = rotwk;
		installs[1].label = "bfme2";
		installs[1].root = bfme2;
		REQUIRE_MESSAGE(RetailArchivePolicy::loadBuiltin("rotwk-201", installs[0].policy, &err), err);
		REQUIRE_MESSAGE(RetailArchivePolicy::loadBuiltin("bfme2-106", installs[1].policy, &err), err);
		RetailMountOptions opts;
		opts.md5CachePath = (std::filesystem::temp_directory_path() / "openbfme-local55-md5.tsv").u8string();
		const RetailMountReport retail = MountRetailArchives(pure, installs, opts);
		REQUIRE_MESSAGE(retail.ok, "retail mount failed");
		ModMountOptions modOpts;
		modOpts.rotwkInstallRoot = rotwk;
		modOpts.md5CachePath = opts.md5CachePath;
		const ModMountReport mounted = MountInstallOverlay(merged, retail, mod, modOpts);
		REQUIRE_MESSAGE(mounted.ok, "Ennorath overlay mount failed");
		loaded = true;
		return true;
	}
};

EnnorathMount &sharedEnnorathMount()
{
	static EnnorathMount m;
	return m;
}
} // namespace

TEST_CASE("LOCAL-55 P-93 check box sounds: hover and left-click release play Gui_ShellMapMouseOver, Gui_ShellMapMouseOut and Gui_ShellMapSelect; right-click and Space/Enter stay silent (RW 0x72937E)")
{
	SynthAudioRig rig;
	GameWindowManager gwm;

	WinInstanceData tracked;
	tracked.m_style = GWS_CHECK_BOX | GWS_MOUSE_TRACK;
	GameWindow *box = gwm.gogoGadgetCheckbox(nullptr, WIN_STATUS_ENABLED, 10, 10, 120, 30, &tracked, nullptr, false);
	REQUIRE(box);

	// Enter -> Gui_ShellMapMouseOver (RW 0x7294F4 -> 0x729522 -> 0x815772)
	CHECK(gwm.winSendInputMsg(box, GWM_MOUSE_ENTERING, 0, 0) == MSG_HANDLED);
	CHECK(rig.takeRequestedEvents() == std::vector<std::string>{ "Gui_ShellMapMouseOver" });

	// Left press -> silent (RW 0x72939D -> 0x72952D)
	CHECK(gwm.winSendInputMsg(box, GWM_LEFT_DOWN, 0, 0) == MSG_HANDLED);
	CHECK(rig.takeRequestedEvents().empty());

	// Left release while hilited -> toggles and plays Gui_ShellMapSelect (RW 0x7293C9 -> 0x7293FA -> 0x815772)
	CHECK(gwm.winSendInputMsg(box, GWM_LEFT_UP, 0, 0) == MSG_HANDLED);
	CHECK(GadgetCheckBoxIsChecked(box));
	CHECK(rig.takeRequestedEvents() == std::vector<std::string>{ "Gui_ShellMapSelect" });

	// Leave -> Gui_ShellMapMouseOut (RW 0x7294BF -> 0x7294ED -> 0x815772)
	CHECK(gwm.winSendInputMsg(box, GWM_MOUSE_LEAVING, 0, 0) == MSG_HANDLED);
	CHECK(rig.takeRequestedEvents() == std::vector<std::string>{ "Gui_ShellMapMouseOut" });

	// Left release while not hilited -> ignored, silent (RW 0x7293CC -> 0x729442)
	CHECK(gwm.winSendInputMsg(box, GWM_LEFT_UP, 0, 0) == MSG_IGNORED);
	CHECK(GadgetCheckBoxIsChecked(box));
	CHECK(rig.takeRequestedEvents().empty());

	// Right release while selected -> clears selection, silent (RW 0x729404)
	CHECK(gwm.winSendInputMsg(box, GWM_RIGHT_UP, 0, 0) == MSG_HANDLED);
	CHECK_FALSE(GadgetCheckBoxIsChecked(box));
	CHECK(rig.takeRequestedEvents().empty());

	// Keyboard Space / Enter -> toggles, silent (RW 0x729449)
	CHECK(gwm.winSendInputMsg(box, GWM_CHAR, KEY_SPACE, KEY_STATE_DOWN) == MSG_HANDLED);
	CHECK(GadgetCheckBoxIsChecked(box));
	CHECK(gwm.winSendInputMsg(box, GWM_CHAR, KEY_ENTER, KEY_STATE_DOWN) == MSG_HANDLED);
	CHECK_FALSE(GadgetCheckBoxIsChecked(box));
	CHECK(rig.takeRequestedEvents().empty());

	CHECK(rig.countFileStarts("cb_over") == 1);
	CHECK(rig.countFileStarts("cb_select") == 1);
	CHECK(rig.countFileStarts("cb_out") == 1);

	// Without GWS_MOUSE_TRACK: hover enter/leave do not hilite or play hover sounds (RW 0x7294BF / 0x7294F4)
	WinInstanceData untracked;
	untracked.m_style = GWS_CHECK_BOX;
	GameWindow *plain = gwm.gogoGadgetCheckbox(nullptr, WIN_STATUS_ENABLED, 10, 60, 120, 30, &untracked, nullptr, false);
	REQUIRE(plain);
	CHECK(gwm.winSendInputMsg(plain, GWM_MOUSE_ENTERING, 0, 0) == MSG_HANDLED);
	CHECK(gwm.winSendInputMsg(plain, GWM_MOUSE_LEAVING, 0, 0) == MSG_HANDLED);
	CHECK(rig.takeRequestedEvents().empty());
}

TEST_CASE("LOCAL-55 ENNORATH scenario P-93: MpGameSetup.apt Rules tab Allow Custom Heroes and Allow Ring Heroes toggles play Gui_ShellMapSelect once per press and Gui_ShellMapMouseOver on hover (RW 0x72937E)")
{
	EnnorathMount &mount = sharedEnnorathMount();
	if (!mount.load("LOCAL-55 skirmish check box sounds"))
	{
		return;
	}
	Win32BIGFileSystem &fs = mount.merged;

	AudioIniState audioIni;
	INIEnvironment audioEnv;
	audioEnv.fileSystem = &fs;
	audioIni.registerBlocks(audioEnv.blocks);
	{
		INI ini(audioEnv);
		REQUIRE_NOTHROW(audioIni.loadAll(ini));
	}
	AudioAssetCache cache(&fs, 8u << 20);
	SimulatedAudioDevice device(&cache);
	AudioManager audio(audioIni, cache, device, RandomAlgorithm::RotWK_GameDat_LCG, 1);
	audio.enableEventLog(true);
	double audioTime = 0.0;
	audio.update(audioTime);
	struct AudioGuard
	{
		explicit AudioGuard(AudioManager *m) { AudioApi::install(m); }
		~AudioGuard() { AudioApi::install(nullptr); }
	} audioGuard(&audio);

	AptArchiveFileSource source(fs);
	RecordingShellServices services;
	RulesLabelsText text;
	fillLobbyLabels(text);
	IniSkirmishSetupSource setup;
	std::string err;
	REQUIRE_MESSAGE(setup.load(fs, &err), err);
	RecordingNewGameSink sink;
	ShellEnvironment env;
	env.gameText = &text;
	env.skirmish = &setup;
	env.newGame = &sink;

	GadgetSkinData skins;
	REQUIRE_MESSAGE(loadGadgetSkinData(fs, skins, &err), err);
	AptScreenFactoryTable factories;
	registerAptScreenFactories(factories);
	auto wm = std::make_unique<WindowManager>(source, services);
	auto layer = std::make_unique<AptGadgetLayer>(*wm, source, skins);
	layer->registerComponents();
	auto shell = std::make_unique<Shell>(*wm, factories, services, env);
	wm->init();

	shell->push("Skirmish.apt");
	AptSkirmish *screen = dynamic_cast<AptSkirmish *>(shell->top());
	REQUIRE(screen);
	auto tick = [&](int n) {
		for (int i = 0; i < n; ++i)
		{
			wm->update(33);
		}
	};
	tick(60);

	// Create a profile and switch MpGameSetup.apt to the Rules tab
	wm->invokeCallback("AptSkirmish::OnNewProfileMenu", "");
	REQUIRE(screen->profileEntryWindow());
	GadgetTextEntrySetText(screen->profileEntryWindow(), u"Gimli");
	wm->invokeCallback("AptSkirmish::OnAddProfileAccept", "");
	REQUIRE(wm->invokeASAt(1, "ProfilePopup", "ClosePop", {}));
	tick(30);
	AptButtonInst *rulesTab = firstButtonIn(wm->apt().resolvePath(wm->apt().level(1), "OpenPlay.MpMapAndSettings.TabButtons.RulesTab"));
	REQUIRE(rulesTab);
	float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
	REQUIRE(rulesTab->contentBounds(x0, y0, x1, y1));
	float tx = 0, ty = 0;
	rulesTab->globalMatrix().apply((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, tx, ty);
	wm->postMouseMove(tx, ty);
	tick(1);
	wm->postMouseButton(true);
	tick(1);
	wm->postMouseButton(false);
	tick(30);

	REQUIRE(screen->rulesPopulated());
	GameWindow *customHeroes = screen->ruleWindow(RULE_ALLOW_CUSTOM_HEROES);
	GameWindow *ringHeroes = screen->ruleWindow(RULE_ALLOW_RING_HEROES);
	REQUIRE(customHeroes);
	REQUIRE(ringHeroes);
	CHECK(customHeroes == screen->ruleCheckWindow(0));
	CHECK(ringHeroes == screen->ruleCheckWindow(1));
	CHECK_FALSE(customHeroes->winIsHidden());
	CHECK_FALSE(ringHeroes->winIsHidden());
	CHECK(GadgetCheckBoxIsChecked(customHeroes));
	CHECK(GadgetCheckBoxIsChecked(ringHeroes));

	// Park the pointer away and flush any setup-time audio events
	wm->postMouseMove(5.0f, 5.0f);
	tick(2);
	audioTime += 200.0;
	device.setTime(audioTime);
	audio.update(audioTime);
	(void)audio.takeEventLog();
	size_t devPos = device.log().size();

	auto drainUiEvents = [&]() {
		audioTime += 200.0;
		device.setTime(audioTime);
		audio.update(audioTime);
		std::vector<std::string> reqs;
		for (const AudioLogEntry &e : audio.takeEventLog())
		{
			if (!e.play)
			{
				reqs.push_back(e.event);
			}
		}
		std::vector<std::string> files;
		const auto &dlog = device.log();
		while (devPos < dlog.size())
		{
			if (dlog[devPos].kind == SimulatedAudioDevice::LogEntry::Start)
			{
				files.push_back(lowerAscii(dlog[devPos].file));
			}
			++devPos;
		}
		return std::pair<std::vector<std::string>, std::vector<std::string>>{ std::move(reqs), std::move(files) };
	};

	auto checkToggle = [&](GameWindow *box, GameRuleId rule) {
		INFO("rule=" << (int)rule);
		int sx = 0, sy = 0, sw = 0, sh = 0;
		box->winGetScreenPosition(&sx, &sy);
		box->winGetSize(&sw, &sh);
		const float cx = (float)sx + (float)sw * 0.5f;
		const float cy = (float)sy + (float)sh * 0.5f;

		// Hover on -> Gui_ShellMapMouseOver (Ennorath soundeffects.ini: UMouseoverb)
		wm->postMouseMove(cx, cy);
		tick(1);
		{
			auto [reqs, files] = drainUiEvents();
			CHECK(reqs == std::vector<std::string>{ "Gui_ShellMapMouseOver" });
			REQUIRE(files.size() == 1);
			CHECK(files[0].find("umouseoverb") != std::string::npos);
		}

		// First press -> off, release plays Gui_ShellMapSelect once (Ennorath soundeffects.ini: UClick_jewel)
		wm->postMouseButton(true);
		tick(1);
		CHECK(drainUiEvents().first.empty());
		wm->postMouseButton(false);
		tick(1);
		CHECK(screen->setup()->info().rules[rule] == 0);
		CHECK_FALSE(GadgetCheckBoxIsChecked(box));
		{
			auto [reqs, files] = drainUiEvents();
			CHECK(reqs == std::vector<std::string>{ "Gui_ShellMapSelect" });
			REQUIRE(files.size() == 1);
			CHECK(files[0].find("uclick_jewel") != std::string::npos);
		}

		// Second press -> back on, release plays Gui_ShellMapSelect once
		wm->postMouseButton(true);
		tick(1);
		CHECK(drainUiEvents().first.empty());
		wm->postMouseButton(false);
		tick(1);
		CHECK(screen->setup()->info().rules[rule] == 1);
		CHECK(GadgetCheckBoxIsChecked(box));
		{
			auto [reqs, files] = drainUiEvents();
			CHECK(reqs == std::vector<std::string>{ "Gui_ShellMapSelect" });
			REQUIRE(files.size() == 1);
			CHECK(files[0].find("uclick_jewel") != std::string::npos);
		}

		// Hover off -> Gui_ShellMapMouseOut requested (Ennorath soundeffects.ini has Sounds commented out, so 0 voices started)
		wm->postMouseMove(5.0f, 5.0f);
		tick(1);
		{
			auto [reqs, files] = drainUiEvents();
			CHECK(reqs == std::vector<std::string>{ "Gui_ShellMapMouseOut" });
			CHECK(files.empty());
		}
	};

	checkToggle(customHeroes, RULE_ALLOW_CUSTOM_HEROES);
	checkToggle(ringHeroes, RULE_ALLOW_RING_HEROES);

	shell.reset();
	layer.reset();
}
