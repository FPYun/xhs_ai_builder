param(
    [string]$Sketch = ".\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino",
    [string]$PlayerFlowDoc = ".\docs\player-flow-ui.md",
    [string]$SdPayload = ".\sd_card_payload",
    [string]$SdBoundaryDoc = ".\docs\sd-card-file-boundary.md",
    [string]$DeviceAcceptanceDoc = ".\docs\device-acceptance.md",
    [switch]$SkipGit
)

$ErrorActionPreference = "Stop"

$Failures = @()

function Add-Pass {
    param([string]$Message)
    Write-Host "[PASS] $Message"
}

function Add-Fail {
    param([string]$Message)
    $script:Failures += $Message
    Write-Host "[FAIL] $Message"
}

function Read-Utf8 {
    param([string]$Path)

    if (!(Test-Path $Path)) {
        throw "File not found: $Path"
    }

    $Resolved = Resolve-Path $Path
    return [IO.File]::ReadAllText($Resolved, [Text.Encoding]::UTF8)
}

$SketchText = Read-Utf8 $Sketch
$DocText = Read-Utf8 $PlayerFlowDoc
$SdBoundaryText = Read-Utf8 $SdBoundaryDoc
$DeviceAcceptanceText = if (Test-Path $DeviceAcceptanceDoc) { Read-Utf8 $DeviceAcceptanceDoc } else { "" }

$EngineeringPattern = [regex]"HOST|CLIENT|UDP|TX|RX|PEER|SYNC|READY|F:"
$DisplayHits = @()
$Lines = $SketchText -split "`r?`n"
for ($Index = 0; $Index -lt $Lines.Count; $Index++) {
    $Line = $Lines[$Index]
    $IsPlayerVisible = $Line.Contains("Display.print") -or
        $Line.Contains("Display.printf") -or
        $Line.Contains("draw_action_footer") -or
        $Line.Contains("draw_game_title")

    if ($IsPlayerVisible -and $EngineeringPattern.IsMatch($Line) -and !$Line.Contains("Serial")) {
        $DisplayHits += "{0}: {1}" -f ($Index + 1), $Line.Trim()
    }
}

if ($DisplayHits.Count -eq 0) {
    Add-Pass "player-visible display text hides low-level link terms"
} else {
    Add-Fail "player-visible display text exposes low-level terms:"
    $DisplayHits | ForEach-Object { Write-Host "       $_" }
}

$AppIdleUsesGameText = [regex]::IsMatch($SketchText, "maybeAction\('idle'\).*\u4f11\u95f2")
$AppIdleUsesStaleText = [regex]::IsMatch($SketchText, "maybeAction\('idle'\).*\u5f85\u673a")
$AppMatchLeaksTransport = $SketchText.Contains("CoreS3 Wi-Fi AP")
$AppLeaksStorageDetail = $SketchText.Contains("RAM")
$AppModeBanner = $SketchText.Contains("renderModeBanner(s)") -and
    $SketchText.Contains("modebanner") -and
    $SketchText.Contains("mode-capture-fail") -and
    $SketchText.Contains("s.screenLabel||s.screen")
$DynamicBattleScreenLabel = $SketchText.Contains("case kScreenMatch: return app_battle_phase_label();") -and
    $SketchText.Contains("case kScreenBattle: return app_battle_phase_label();") -and
    $DocText.Contains("screenLabel follows battle phase")
if ($AppIdleUsesGameText -and $AppModeBanner -and $DynamicBattleScreenLabel -and (-not $AppIdleUsesStaleText) -and (-not $AppMatchLeaksTransport) -and (-not $AppLeaksStorageDetail)) {
    Add-Pass "App player text uses game-state wording, visual mode banner, and hides low-level match transport"
} else {
    Add-Fail "App player text still exposes stale idle wording, misses visual mode banner or dynamic battle labels, leaks low-level match transport, or storage internals"
}

$DeviceTitlePlate = $SketchText.Contains("CoreS3.Display.textWidth(title)") -and
    $SketchText.Contains("fillRoundRect(x - 8, y - 5") -and
    $SketchText.Contains("drawRoundRect(x - 8, y - 5")
if ($DeviceTitlePlate) {
    Add-Pass "device screen titles use a game-state title plate"
} else {
    Add-Fail "device screen titles are missing the game-state title plate"
}

$AppHomeActionPanel = $SketchText.Contains(".actionpanel") -and
    $SketchText.Contains("function renderActionPanel(s)") -and
    $SketchText.Contains("const recent=peerShort(b.recentPeerId)") -and
    $SketchText.Contains("b.rematchAvailable?") -and
    $SketchText.Contains("renderActionPanel(s)") -and
    $DocText.Contains("App home action panel")
if ($AppHomeActionPanel) {
    Add-Pass "App home page shows a game-like action panel with friendship and rematch cues"
} else {
    Add-Fail "App home page is missing action panel, recent opponent, friendship/rematch cues, or documentation"
}

$PublicTypeMarkers = @(
    "enum ElementType",
    "struct PetGenes",
    "struct SavedPet",
    "struct BackpackStorage",
    "enum ObjectClass",
    "struct BattlePetPacket"
)
$RedefinedTypes = $PublicTypeMarkers | Where-Object { $SketchText.Contains($_) }
if ($RedefinedTypes.Count -eq 0) {
    Add-Pass "sketch does not redefine public shared types"
} else {
    Add-Fail ("sketch redefines public shared types: " + ($RedefinedTypes -join ", "))
}

if (!$SketchText.Contains("Say PAIZHAO")) {
    Add-Pass "voice-recognition prompt is absent"
} else {
    Add-Fail "voice-recognition prompt is present"
}

if ($SketchText.Contains("kAudioMuted") -and $SketchText.Contains("play_scene_sound(")) {
    Add-Pass "sound gate and scene sound entry point are present"
} else {
    Add-Fail "missing kAudioMuted or play_scene_sound"
}

$CaptureFailSound = $SketchText.Contains("static void play_capture_fail_sound(bool bagFullFail)") -and
    $SketchText.Contains("play_capture_fail_sound(bagFullFail)") -and
    $SketchText.Contains("play_scene_sound(kSoundWarning);") -and
    $SketchText.Contains("play_scene_sound(kSoundBag);") -and
    $DocText.Contains("Full-bag capture fail")
if ($CaptureFailSound) {
    Add-Pass "capture failure sound distinguishes full-bag next step through scene sound routing"
} else {
    Add-Fail "capture failure sound does not distinguish full-bag guidance or documentation"
}

$MatchPrepareSound = $SketchText.Contains("static void play_match_prepare_sound(bool hasStoredPet)") -and
    $SketchText.Contains("play_match_prepare_sound(backpack.count > 0)") -and
    $SketchText.Contains("play_scene_sound(hasStoredPet ? kSoundBag : kSoundPhoto)") -and
    $DocText.Contains("MATCH entered without active pet")
if ($MatchPrepareSound) {
    Add-Pass "MATCH preparation sound points to BAG or PHOTO through scene sound routing"
} else {
    Add-Fail "MATCH preparation sound does not point to BAG/PHOTO or documentation"
}

if ($SketchText.Contains("xp_to_level_target(") -and
    $SketchText.Contains("format_growth_goal(") -and
    $SketchText.Contains("draw_growth_goal_badge(") -and
    $SketchText.Contains('CoreS3.Display.print("NEXT")') -and
    $SketchText.Contains("draw_growth_goal_badge(16, 82, 150, pet") -and
    $SketchText.Contains("draw_growth_goal_badge(178, 48, 124, pet") -and
    $SketchText.Contains("pet.level < 5") -and
    $SketchText.Contains("pet.level < 12") -and
    $SketchText.Contains("pet_growth_boost(") -and
    $SketchText.Contains("pet_battle_power(") -and
    $SketchText.Contains("pet_battle_agility(") -and
    $SketchText.Contains("pet_battle_spirit(") -and
    $SketchText.Contains("format_growth_goal(goal, sizeof(goal), pet)") -and
    $SketchText.Contains("stats=%u/%u/%u") -and
    $SketchText.Contains('out += ",\"growthGoal\":"') -and
    $SketchText.Contains("p.growthGoal") -and
    $SketchText.Contains('class="goal"') -and
    $SketchText.Contains(".goal{") -and
    $SketchText.Contains("goal=%s")) {
    Add-Pass "BAG, capture-success, App, and serial expose growth targets and boosted battle stats"
} else {
    Add-Fail "BAG, capture-success, App, or serial is missing growth targets or boosted battle stats"
}

$EmptyBagStyles = $SketchText.Contains(".emptybag{") -or
    $SketchText.Contains(".emptybag,.emptypet{") -or
    $SketchText.Contains(".bagoverview,.emptybag")
$EmptyBagRendered = $SketchText.Contains("if(!bagPets.length)return renderEmptyBag()") -or
    $SketchText.Contains("if(!bagPets.length)return renderBagOverview(b)+renderEmptyBag()")
$AppEmptyBagCard = $EmptyBagStyles -and
    $SketchText.Contains("function renderEmptyBag()") -and
    $EmptyBagRendered -and
    $SketchText.Contains("0/6") -and
    $SketchText.Contains("maybeAction('photo')") -and
    $SketchText.Contains("go('capture')") -and
    $DocText.Contains("App empty BAG first-capture card")
if ($AppEmptyBagCard) {
    Add-Pass "App empty BAG uses a first-capture card with PHOTO next step"
} else {
    Add-Fail "App empty BAG is missing a first-capture card, 0/6 capacity, PHOTO action, or documentation"
}

$AppBagOverviewCard = ($SketchText.Contains(".bagoverview{") -or $SketchText.Contains(".bagoverview,.emptybag")) -and
    $SketchText.Contains("function renderBagOverview(b)") -and
    $SketchText.Contains("const pets=b.pets||bagPets||[]") -and
    $SketchText.Contains("i===selected?'active'") -and
    $SketchText.Contains("i===cursor?'cursor'") -and
    $SketchText.Contains("renderBagOverview(b)+renderEmptyBag()") -and
    $SketchText.Contains('renderBagOverview(b)+`<div class="swipehint"') -and
    $DocText.Contains("App bag overview card")
if ($AppBagOverviewCard) {
    Add-Pass "App BAG shows capacity, active slot, and cursor overview"
} else {
    Add-Fail "App BAG is missing overview card, active/cursor slots, or documentation"
}

$AppNoCurrentPetCard = $SketchText.Contains(".emptypet{") -and
    $SketchText.Contains("function renderNoCurrentPet()") -and
    $SketchText.Contains("if(!p)return renderNoCurrentPet()") -and
    $SketchText.Contains("const hasStored=count>0") -and
    $SketchText.Contains("maybeAction('bag')") -and
    $SketchText.Contains("maybeAction('photo')") -and
    $DocText.Contains("App current-pet empty card")
if ($AppNoCurrentPetCard) {
    Add-Pass "App current-pet empty state points to BAG or PHOTO"
} else {
    Add-Fail "App current-pet empty state is missing a graphical card, BAG/PHOTO actions, or documentation"
}

$AppPetGrowthWaitRail = $SketchText.Contains('out += ",\"growthWaitSec\":"') -and
    $SketchText.Contains('out += ",\"growthProgress\":"') -and
    $SketchText.Contains('out += ",\"growthIntervalSec\":"') -and
    $SketchText.Contains("const waitMax=Math.max(1,num(p.growthIntervalSec)||30)") -and
    $SketchText.Contains("stat('等待成长',wait,waitMax)") -and
    $SketchText.Contains('等待 ${h(waitSec)}s') -and
    $DocText.Contains("App pet growth wait rail")
if ($AppPetGrowthWaitRail) {
    Add-Pass "App pet cards show waiting-growth countdown and rail"
} else {
    Add-Fail "App pet cards are missing waiting-growth JSON, countdown rail, or documentation"
}

$AppTrainingPlanCard = $SketchText.Contains(".trainingplan") -and
    $SketchText.Contains("function renderTrainingPlanCard(s)") -and
    $SketchText.Contains("const p=s.currentPet||{}") -and
    $SketchText.Contains("p.growthGoal") -and
    $SketchText.Contains("p.growthWaitSec") -and
    $SketchText.Contains("maybeAction('photo')") -and
    $SketchText.Contains("maybeAction('match')") -and
    $SketchText.Contains("renderPet(s.currentPet)+renderTrainingPlanCard(s)") -and
    $DocText.Contains("App training plan card")
if ($AppTrainingPlanCard) {
    Add-Pass "App current-pet view shows training plan and growth sources"
} else {
    Add-Fail "App current-pet view is missing training plan card, growth target/sources, or documentation"
}

$ReleaseConfirmSummary = $SketchText.Contains("draw_release_confirm_summary(") -and
    $SketchText.Contains("draw_release_confirm_summary(156, 102, 144, pet, bag_cursor, backpack.count") -and
    $SketchText.Contains("draw_growth_goal_badge(156, 128, 144, pet, pet.genes.accentColor)") -and
    $SketchText.Contains("win_rate_percent(pet)") -and
    $DocText.Contains("release confirmation summary strip")
if ($ReleaseConfirmSummary) {
    Add-Pass "RELEASE_CONFIRM shows a compact pet summary and growth target before deletion"
} else {
    Add-Fail "RELEASE_CONFIRM is missing the pet summary strip, growth target, or matching documentation"
}

$CaptureRecognizedHint = [regex]::IsMatch($SketchText, "\u5df2\u53d1\u73b0")
$CaptureFullBagHint = [regex]::IsMatch($SketchText, "\u80cc\u5305\u5df2\u6ee1.*\u653e\u751f")
$CaptureRetryHint = [regex]::IsMatch($SketchText, "\u4e3b\u4f53\u653e\u8fdb\u753b\u9762\u4e2d\u592e")
$CaptureProgressBar = $SketchText.Contains("num(r.confidence),100")
if ($SketchText.Contains("function captureHint(r)") -and
    $SketchText.Contains("capture_failure_label(") -and
    $SketchText.Contains("draw_capture_quality_panel(") -and
    $SketchText.Contains("draw_capture_quality_panel(16, 148, recog.presenceScore, recog.confidence)") -and
    $SketchText.Contains("draw_capture_quality_panel(8, 124, recog.presenceScore, recog.confidence)") -and
    $SketchText.Contains("subjectScore, 100, TFT_YELLOW") -and
    $SketchText.Contains("recognitionScore, 100, TFT_RED") -and
    $SketchText.Contains("function failureText(reason)") -and
    $SketchText.Contains("'Model distance high':") -and
    $SketchText.Contains("failureText(r.failureReason)") -and
    $CaptureRecognizedHint -and
    $CaptureFullBagHint -and
    $CaptureRetryHint -and
    $CaptureProgressBar -and
    $DocText.Contains("device wild capture quality panel")) {
    Add-Pass "Capture UI gives recognized, full-bag, retry, and device quality guidance"
} else {
    Add-Fail "Capture UI is missing next-step guidance, recognition progress, or device quality guidance"
}

$AppCaptureQualityCard = ($SketchText.Contains(".capturequality{") -or $SketchText.Contains(".capturequality.ready")) -and
    $SketchText.Contains("function renderCaptureQualityCard(r)") -and
    $SketchText.Contains("const subject=Math.max(0,Math.min(100,num(r.presenceScore)))") -and
    $SketchText.Contains("const recog=Math.max(0,Math.min(100,num(r.confidence)))") -and
    $SketchText.Contains("r.recognized") -and
    $SketchText.Contains("maybeAction(ok?'capture':'photo')") -and
    $SketchText.Contains("renderCaptureQualityCard(r)+captureHint(r)") -and
    $DocText.Contains("App capture quality card")
if ($AppCaptureQualityCard) {
    Add-Pass "App capture page shows a graphical capture quality card"
} else {
    Add-Fail "App capture page is missing graphical quality card, action guidance, or documentation"
}

if ($SketchText.Contains("kExternalSoundSampleRate") -and
    $SketchText.Contains("SD.open(path, FILE_READ)") -and
    $SketchText.Contains("malloc(fileSize)") -and
    $SketchText.Contains("free(audioBuffer)") -and
    $SketchText.Contains("CoreS3.Speaker.playRaw(audioBuffer") -and
    $SketchText.Contains("return played")) {
    Add-Pass "optional SD raw sound assets use temporary buffers and fall back through scene sound routing"
} else {
    Add-Fail "optional SD raw sound asset playback is missing, leaks buffers, or bypasses scene sound routing"
}

$ExpectedSdAudio = @(
    "audio\ui\idle.raw",
    "audio\ui\photo.raw",
    "audio\ui\wild.raw",
    "audio\ui\bag.raw",
    "audio\ui\capture.raw",
    "audio\ui\release.raw",
    "audio\ui\select.raw",
    "audio\ui\warning.raw",
    "audio\ui\level_up.raw",
    "audio\ui\cancel.raw",
    "audio\battle\match.raw",
    "audio\battle\clash.raw",
    "audio\battle\friend.raw",
    "audio\battle\win.raw",
    "audio\battle\draw.raw",
    "audio\battle\lose.raw",
    "audio\battle\exit.raw",
    "audio\music\intro.raw"
)
$MissingSdAudio = @()
$InvalidSdAudio = @()
foreach ($RelPath in $ExpectedSdAudio) {
    $FullPath = Join-Path $SdPayload $RelPath
    if (!(Test-Path $FullPath)) {
        $MissingSdAudio += $RelPath
        continue
    }
    $Length = (Get-Item $FullPath).Length
    if ($Length -le 0 -or $Length -gt 32768) {
        $InvalidSdAudio += "$RelPath=$Length"
    }
}
$SdPayloadReady = $MissingSdAudio.Count -eq 0 -and
    $InvalidSdAudio.Count -eq 0 -and
    $SdBoundaryText.Contains("SD 音频已经实现为可选覆盖") -and
    !$SdBoundaryText.Contains("读取播放尚未实现")
if ($SdPayloadReady) {
    Add-Pass "SD card payload includes all optional raw sound assets and current boundary docs"
} else {
    $Reasons = @()
    if ($MissingSdAudio.Count -gt 0) { $Reasons += "missing: $($MissingSdAudio -join ', ')" }
    if ($InvalidSdAudio.Count -gt 0) { $Reasons += "invalid-size: $($InvalidSdAudio -join ', ')" }
    if (!$SdBoundaryText.Contains("SD 音频已经实现为可选覆盖")) { $Reasons += "missing current SD audio doc" }
    if ($SdBoundaryText.Contains("读取播放尚未实现")) { $Reasons += "stale SD audio doc" }
    Add-Fail ("SD card optional sound payload or boundary docs are incomplete: " + ($Reasons -join "; "))
}

if ($SketchText.Contains("friendship_goal_badge()") -and
    $SketchText.Contains("add_current_friend(") -and
    $SketchText.Contains("show_friend_action_feedback(message)") -and
    $SketchText.Contains("current_friend_candidate_id()") -and
    $SketchText.Contains("buttonActions") -and
    $SketchText.Contains("bagActions") -and
    $SketchText.Contains("battleActions") -and
    ($SketchText.Contains("act('friend')") -or $SketchText.Contains("maybeAction('friend')")) -and
    $SketchText.Contains('type == "friend"') -and
    $SketchText.Contains("pairingStepLabel") -and
    $SketchText.Contains("waitingSec") -and
    $SketchText.Contains("nextRematchXp") -and
    $SketchText.Contains("nextFriendshipGain") -and
    $SketchText.Contains("rematchAvailable") -and
    $SketchText.Contains("peerShort(") -and
    $SketchText.Contains("last_friend_peer_id != 0") -and
    $SketchText.Contains("next_friendship_score_gain()") -and
    $SketchText.Contains("friendship_rematch_window_open()") -and
    $SketchText.Contains("next_friendship_projected_rank()") -and
    $SketchText.Contains("maybe_play_friendship_hint_sound()") -and
    $SketchText.Contains("last_friendship_hint_sound_rank") -and
    $SketchText.Contains("draw_friendship_badge(") -and
    $SketchText.Contains("fillCircle(x + 9") -and
    $SketchText.Contains("draw_friendship_badge(212, 78, 90)") -and
    $SketchText.Contains("draw_friendship_badge(218, static_cast<int16_t>(infoLine1Y - 4), 82)") -and
    $SketchText.Contains("last_friend_bond_up || app_last_battle_level_up") -and
    $SketchText.Contains("next_rematch_xp_bonus()") -and
    $SketchText.Contains("static Preferences friend_prefs;") -and
    $SketchText.Contains('friend_prefs.begin("wuxingfr", false)') -and
    $SketchText.Contains("static void save_friendship_state()") -and
    $SketchText.Contains("static void load_friendship_state()") -and
    $SketchText.Contains('friend_prefs.putBytes("friends", local_friends, sizeof(local_friends))') -and
    $SketchText.Contains("load_friendship_state();") -and
    $SketchText.Contains("save_friendship_state();") -and
    $DocText.Contains("wuxingfr") -and
    $SketchText.Contains("friendship_color()") -and
    $SketchText.Contains("friend_bond_rank(friend_score)") -and
    $SketchText.Contains("friendship_score(), 100, friendship_color()") -and
    $SketchText.Contains("function bondMeter(score)") -and
    $SketchText.Contains("bondMeter(b.friendshipScore)") -and
    $SketchText.Contains("const bond=bondMeter(b.friendshipScore)") -and
    $SketchText.Contains("bondMeter(f.score)") -and
    $SketchText.Contains("button_label_for_slot(kButtonLeft)") -and
    $SketchText.Contains("button_label_for_slot(kButtonMiddle)") -and
    $SketchText.Contains("button_label_for_slot(kButtonRight)") -and
    $SketchText.Contains("app_action_type_for_button(kButtonLeft)") -and
    $SketchText.Contains("app_action_type_for_button(kButtonMiddle)") -and
    $SketchText.Contains("app_action_type_for_button(kButtonRight)")) {
    Add-Pass "status/App bridge exposes friendship goal, rematch reward, labels, action IDs, and player-facing pairing state"
} else {
    Add-Fail "status/App bridge is missing friendship goal, rematch reward, button labels, action IDs, or player-facing pairing state"
}

$AppFriendEmptyCard = ($SketchText.Contains(".friendempty{") -or $SketchText.Contains(".friendcard,.friendempty")) -and
    $SketchText.Contains("function renderFriendEmpty(b)") -and
    $SketchText.Contains("if(!list.length)return html+renderFriendEmpty(b)") -and
    $SketchText.Contains("peerShort(b.recentPeerId)") -and
    $SketchText.Contains("maybeAction('friend')") -and
    $SketchText.Contains("maybeAction('match')") -and
    $DocText.Contains("App friend empty card")
if ($AppFriendEmptyCard) {
    Add-Pass "App friend empty state offers add-friend and rematch actions"
} else {
    Add-Fail "App friend empty state is missing a graphical card, recent opponent, add-friend/match actions, or documentation"
}

$AppFriendGoalCard = ($SketchText.Contains(".friendgoal{") -or $SketchText.Contains(".friendgoal,.friendcard")) -and
    $SketchText.Contains("function renderFriendGoalCard(b)") -and
    $SketchText.Contains("const nextXp=b.rematchAvailable?") -and
    $SketchText.Contains("b.nextRematchXp") -and
    $SketchText.Contains("b.nextFriendshipGain") -and
    $SketchText.Contains("friendGoal(b.friendshipScore)") -and
    $SketchText.Contains("maybeAction('match')") -and
    $SketchText.Contains("maybeAction('friend')") -and
    $SketchText.Contains("renderFriendGoalCard(b)+") -and
    $DocText.Contains("App friend goal card")
if ($AppFriendGoalCard) {
    Add-Pass "App friendship page shows goal, rematch reward, and add-friend actions"
} else {
    Add-Fail "App friendship page is missing goal/reward card, actions, or documentation"
}

$AppFriendStreakCard = ($SketchText.Contains(".friendstreak") -or $SketchText.Contains(".friendstreak,.friendgoal")) -and
    $SketchText.Contains("function renderFriendStreakCard(b)") -and
    $SketchText.Contains("const streak=num(b.rematchStreak)") -and
    $SketchText.Contains("const nextFriend=b.nextFriendshipGain?") -and
    $SketchText.Contains("const nextXp=b.rematchAvailable?") -and
    $SketchText.Contains("friendGoal(b.friendshipScore)") -and
    $SketchText.Contains("renderFriendGoalCard(b)+renderFriendStreakCard(b)+") -and
    $DocText.Contains("App friend streak card")
if ($AppFriendStreakCard) {
    Add-Pass "App friendship page shows rematch streak, next rewards, and close-friend progress"
} else {
    Add-Fail "App friendship page is missing rematch streak card, next rewards, close-friend progress, or documentation"
}

if ($SketchText.Contains('type == "confirm_release"') -and
    $SketchText.Contains("screen_mode != kScreenReleaseConfirm") -and
    $SketchText.Contains("screen_mode == kScreenReleaseConfirm") -and
    $SketchText.Contains("release_stored_pet();")) {
    Add-Pass "App and external confirm-release path preserves release confirmation"
} else {
    Add-Fail "confirm_release can bypass or does not clearly preserve release confirmation"
}

if ($SketchText.Contains('type == "next"') -and
    $SketchText.Contains('type == "select"') -and
    $SketchText.Contains('type == "capture"') -and
    $SketchText.Contains('type == "release"') -and
    $SketchText.Contains("screen_mode != kScreenBag") -and
    $SketchText.Contains("screen_mode == kScreenBag") -and
    $SketchText.Contains("screen_mode != kScreenWild") -and
    $SketchText.Contains("screen_mode == kScreenWild") -and
    $SketchText.Contains("actionError") -and
    $SketchText.Contains("send_json_error(400, actionError")) {
    Add-Pass "state-specific App and external actions are guarded with user-facing errors"
} else {
    Add-Fail "state-specific App or external actions can bypass the screen flow or lack user-facing errors"
}

if ($SketchText.Contains("service_serial_control();") -and
    $SketchText.Contains('command == "BTN"') -and
    $SketchText.Contains('command == "ACT"') -and
    $SketchText.Contains('command == "SOUND"') -and
    $SketchText.Contains('command == "SND"') -and
    $SketchText.Contains('command == "STATUS"') -and
    $SketchText.Contains('value == "friend"') -and
    $SketchText.Contains("ACT photo|bag|match|idle") -and
    $SketchText.Contains("confirm_release|cancel|friend") -and
    $SketchText.Contains("add_current_friend(message, sizeof(message))") -and
    $SketchText.Contains("show_friend_action_feedback(message)") -and
    $SketchText.Contains("app_action_from_type(value, &action, &actionError)") -and
    $SketchText.Contains("serial_sound_cue(value, &cue)") -and
    $SketchText.Contains("play_scene_sound(cue)") -and
    $SketchText.Contains("handle_external_button(slot)") -and
    $SketchText.Contains("handle_external_action(action)") -and
    $SketchText.Contains("pet active=") -and
    $SketchText.Contains("stats=%u/%u/%u") -and
    $SketchText.Contains("friend recent=") -and
    $SketchText.Contains("next_xp=%u") -and
    $SketchText.Contains("battle last=")) {
    Add-Pass "USB serial controls reuse guarded paths and expose validation status"
} else {
    Add-Fail "USB serial controls are missing, bypass guarded paths, or lack validation status"
}

$AppSoundPanel = $SketchText.Contains("function renderSoundPanel(s)") -and
    $SketchText.Contains("function testSound(cue)") -and
    $SketchText.Contains('id="soundPanel"') -and
    $SketchText.Contains('type == "sound"') -and
    $SketchText.Contains('String cueName = app_http.arg("cue")') -and
    $SketchText.Contains("serial_sound_cue(cueName, &cue)") -and
    $SketchText.Contains("play_scene_sound(cue)") -and
    $DocText.Contains("App soundboard panel")
if ($AppSoundPanel) {
    Add-Pass "App settings page exposes mute-aware scene sound test panel"
} else {
    Add-Fail "App settings page is missing soundboard panel, HTTP sound action, play_scene_sound routing, or documentation"
}

$AppSoundPanelCueMarkers = @(
    "['idle','休闲']",
    "['photo','拍照']",
    "['wild','遭遇']",
    "['bag','背包']",
    "['capture','收服']",
    "['release','放生']",
    "['select','选择']",
    "['match','匹配']",
    "['clash','交锋']",
    "['friend','友情']",
    "['win','胜利']",
    "['draw','平局']",
    "['lose','失败']",
    "['warning','警告']",
    "['intro','开场']",
    "['level','成长']",
    "['exit','退场']",
    "['cancel','取消']"
)
$MissingAppSoundPanelCues = $AppSoundPanelCueMarkers | Where-Object { -not $SketchText.Contains($_) }
if ($MissingAppSoundPanelCues.Count -eq 0 -and $DocText.Contains("all serial SOUND cues")) {
    Add-Pass "App soundboard covers every serial scene sound cue"
} else {
    Add-Fail ("App soundboard is missing serial sound cue coverage: " + ($MissingAppSoundPanelCues -join ", "))
}

$AppEventFilterPanel = $SketchText.Contains('id="logFilters"') -and
    $SketchText.Contains('id="logDetail"') -and
    $SketchText.Contains("function logCount(k)") -and
    $SketchText.Contains("function renderLogFilters()") -and
    $SketchText.Contains("setLogFilter(k)") -and
    $SketchText.Contains("renderLogs(l.logs)") -and
    $DocText.Contains("App event filter count badges")
if ($AppEventFilterPanel) {
    Add-Pass "App event list shows category filters, details, and count badges"
} else {
    Add-Fail "App event list is missing category filters, detail view, count badges, or documentation"
}

$AppJsonTransportMarkers = @(
    'out += ",\"tx\":"',
    'out += ",\"rx\":"',
    'out += ",\"failures\":"'
)
$AppJsonTransportHits = $AppJsonTransportMarkers | Where-Object { $SketchText.Contains($_) }
if ($AppJsonTransportHits.Count -eq 0) {
    Add-Pass "App battle JSON hides transport packet counters"
} else {
    Add-Fail ("App battle JSON exposes transport counters: " + ($AppJsonTransportHits -join ", "))
}

$AppBattleVerdict = $SketchText.Contains(".battleverdict{") -and
    $SketchText.Contains("function renderBattleVerdict(b)") -and
    $SketchText.Contains("battleverdict ${verdictClass(b.scoreDiff)}") -and
    ($SketchText.Contains("renderBattleVerdict(b)+row(") -or
     $SketchText.Contains("renderBattleVerdict(b)+renderBattleReward(b)+row(") -or
     $SketchText.Contains("renderBattleVerdict(b)+renderBattleReward(b)+renderBattleExitCard(b)+row(")) -and
    $DocText.Contains("App battle verdict card")
if ($AppBattleVerdict) {
    Add-Pass "App battle page mirrors the device settlement verdict card"
} else {
    Add-Fail "App battle page is missing the battle verdict card or documentation"
}

$AppBattlePhaseCard = ($SketchText.Contains(".battlephase{") -or $SketchText.Contains(".battlephase,.battleverdict{") -or $SketchText.Contains(".battlematch,.battlephase,.battleverdict{")) -and
    $SketchText.Contains("function renderBattlePhaseCard(b)") -and
    $SketchText.Contains("const step=Math.max(0,Math.min(2,num(b.pairingStep)))") -and
    $SketchText.Contains("battlephase ${String(b.phase||'finding')}") -and
    ($SketchText.Contains("renderBattlePhaseCard(b)+renderBattleVerdict(b)") -or
     $SketchText.Contains("renderBattlePhaseCard(b)+renderBattleMatchup(b)+renderBattleVerdict(b)") -or
     $SketchText.Contains("renderBattlePhaseCard(b)+renderBattleMatchup(b)+renderBattleRoundTrackCard(b)") -or
     $SketchText.Contains("renderBattlePhaseCard(b)+renderBattleEntryCard(b)+renderBattleMatchup(b)")) -and
    $DocText.Contains("App battle phase card")
if ($AppBattlePhaseCard) {
    Add-Pass "App battle page shows pairing and clash phase card"
} else {
    Add-Fail "App battle page is missing pairing/clash phase card or documentation"
}

$AppMatchReadyCard = ($SketchText.Contains(".matchready") -or $SketchText.Contains(".matchready,.battlephase")) -and
    $SketchText.Contains("function renderMatchReadyCard(b)") -and
    $SketchText.Contains("const me=(lastStatus&&lastStatus.currentPet)||{}") -and
    $SketchText.Contains("peerShort(b.recentPeerId)") -and
    $SketchText.Contains("b.rematchAvailable?") -and
    $SketchText.Contains("maybeAction('match')") -and
    $SketchText.Contains("renderMatchReadyCard(b)+renderBattlePhaseCard(b)") -and
    $DocText.Contains("App match readiness card")
if ($AppMatchReadyCard) {
    Add-Pass "App battle page shows match readiness, recent opponent, and rematch cues"
} else {
    Add-Fail "App battle page is missing match readiness card, recent opponent/rematch cues, or documentation"
}

$AppBattleMatchupCard = ($SketchText.Contains(".battlematch{") -or $SketchText.Contains(".battlematch,.battlephase")) -and
    $SketchText.Contains("function renderBattleMatchup(b)") -and
    $SketchText.Contains("const me=(lastStatus&&lastStatus.currentPet)||{}") -and
    $SketchText.Contains("avatarData(rival)") -and
    $SketchText.Contains('<div class="vs">VS</div>') -and
    ($SketchText.Contains("renderBattlePhaseCard(b)+renderBattleMatchup(b)+renderBattleVerdict(b)") -or
     $SketchText.Contains("renderBattlePhaseCard(b)+renderBattleMatchup(b)+renderBattleRoundTrackCard(b)+renderBattleVerdict(b)") -or
     $SketchText.Contains("renderBattleEntryCard(b)+renderBattleMatchup(b)+renderBattleRoundTrackCard(b)+renderBattleVerdict(b)")) -and
    $DocText.Contains("App battle matchup card")
if ($AppBattleMatchupCard) {
    Add-Pass "App battle page shows local and opponent pet matchup card"
} else {
    Add-Fail "App battle page is missing local/opponent pet matchup card or documentation"
}

$AppBattleEntryCard = $SketchText.Contains(".battleentry") -and
    $SketchText.Contains("function renderBattleEntryCard(b)") -and
    $SketchText.Contains("<span>宠物出场</span>") -and
    $SketchText.Contains("b.opponentSpecies||((b.peerSeen||b.connected)?'对手同步中':'等待对手')") -and
    $SketchText.Contains("renderBattlePhaseCard(b)+renderBattleEntryCard(b)+renderBattleMatchup(b)") -and
    $DocText.Contains("App battle entry card")
if ($AppBattleEntryCard) {
    Add-Pass "App battle page shows pet-entry card before matchup"
} else {
    Add-Fail "App battle page is missing pet-entry card before matchup or documentation"
}

$AppBattleRoundTrackCard = ($SketchText.Contains(".battlerounds") -or $SketchText.Contains(".battlerounds,.battlephase")) -and
    $SketchText.Contains("function renderBattleRoundTrackCard(b)") -and
    $SketchText.Contains("const phase=Math.max(0,Math.min(2,num(b.clashRound)-1))") -and
    $SketchText.Contains("roundChip('力速',b.powerDiff") -and
    $SketchText.Contains("roundChip('五行',b.elementSwing") -and
    $SketchText.Contains("roundChip('气势',b.spiritDiff") -and
    $SketchText.Contains("renderBattleMatchup(b)+renderBattleRoundTrackCard(b)+renderBattleVerdict(b)") -and
    $DocText.Contains("App battle round track card")
if ($AppBattleRoundTrackCard) {
    Add-Pass "App battle page shows three-round battle track"
} else {
    Add-Fail "App battle page is missing three-round track card, current round, score chips, or documentation"
}

$AppBattleRewardCard = ($SketchText.Contains(".battlereward{") -or $SketchText.Contains(".battlereward,.battlematch")) -and
    $SketchText.Contains("function renderBattleReward(b)") -and
    $SketchText.Contains("if(!b.resultValid)return ''") -and
    $SketchText.Contains("const growth=b.stageUp?") -and
    $SketchText.Contains("b.levelUp?") -and
    $SketchText.Contains("b.rematchAvailable?") -and
    $SketchText.Contains("b.nextRematchXp") -and
    ($SketchText.Contains("renderBattlePhaseCard(b)+renderBattleMatchup(b)+renderBattleVerdict(b)+renderBattleReward(b)") -or
     $SketchText.Contains("renderBattlePhaseCard(b)+renderBattleMatchup(b)+renderBattleRoundTrackCard(b)+renderBattleVerdict(b)+renderBattleReward(b)") -or
     $SketchText.Contains("renderBattlePhaseCard(b)+renderBattleEntryCard(b)+renderBattleMatchup(b)+renderBattleRoundTrackCard(b)+renderBattleVerdict(b)+renderBattleReward(b)")) -and
    $DocText.Contains("App battle reward card")
if ($AppBattleRewardCard) {
    Add-Pass "App battle result shows XP, growth, rematch, and friendship reward card"
} else {
    Add-Fail "App battle result is missing reward/growth card or documentation"
}

$AppBattleExitCard = ($SketchText.Contains(".battleexit{") -or $SketchText.Contains(".battleexit,.battlereward")) -and
    $SketchText.Contains("function renderBattleExitCard(b)") -and
    $SketchText.Contains("if(!b.resultValid)return ''") -and
    $SketchText.Contains("b.phase==='exit'") -and
    $SketchText.Contains("maybeAction('idle')") -and
    $SketchText.Contains("maybeAction('match')") -and
    $SketchText.Contains("maybeAction('friend')") -and
    $SketchText.Contains("renderBattleReward(b)+renderBattleExitCard(b)+row(") -and
    $DocText.Contains("App battle exit card")
if ($AppBattleExitCard) {
    Add-Pass "App battle result gives exit cleanup and next-action card"
} else {
    Add-Fail "App battle result is missing exit cleanup card, next actions, or documentation"
}

$AppBattleReplayCard = $SketchText.Contains(".battlereplay{") -and
    $SketchText.Contains("function renderBattleReplays(items)") -and
    $SketchText.Contains("renderBattleReplays(b.replays)") -and
    $SketchText.Contains('out += ",\"replays\":"') -and
    $SketchText.Contains("send_app_battle_replays") -and
    $DocText.Contains("App battle replay history card")
if ($AppBattleReplayCard) {
    Add-Pass "App battle page shows recent battle replay history cards"
} else {
    Add-Fail "App battle page is missing replay history cards, replay JSON, or documentation"
}

$DeviceBattleExitChoiceStrip = $SketchText.Contains("static void draw_battle_exit_choices(") -and
    $SketchText.Contains('draw_battle_exit_choices("整理", "休闲", "捕捉"') -and
    $SketchText.Contains('draw_battle_exit_choice_chip(16, "L", left, color)') -and
    $SketchText.Contains('draw_battle_exit_choice_chip(116, "M", middle, color)') -and
    $SketchText.Contains('draw_battle_exit_choice_chip(216, "R", right, color)') -and
    $DocText.Contains("device-side battle exit choice strip")
if ($DeviceBattleExitChoiceStrip) {
    Add-Pass "Device battle exit page shows a graphical L/M/R choice strip"
} else {
    Add-Fail "Device battle exit page is missing graphical L/M/R choice strip or documentation"
}

$BattleRoundEvidence = @(
    "battle_sync_id(",
    "app_last_battle_id",
    "app_last_opponent_species",
    "app_last_opponent_level,",
    "CoreS3.Display.print(species_name(local_pet))",
    "species_name_by(packet_element(opponent), opponent.species)",
    "play_pet_sound(opponentGenes, opponent.level, opponentStage)",
    "PetGenes localBattleGenes = local_pet",
    "make_battle_packet(localBattleGenes, local_pet_sequence)",
    "draw_pet_avatar(92, 116, localBattleGenes, false)",
    "draw_battle_score_plate(",
    "battle_round_scores_for_phase(",
    'CoreS3.Display.printf("ME %ld"',
    'CoreS3.Display.printf("RIVAL %ld"',
    "draw_battle_impact(",
    'CoreS3.Display.printf("同步局%04lX"',
    "draw_battle_round_summary(",
    "draw_battle_round_summary(18, 124, app_last_battle_power_diff",
    "draw_battle_round_chip(",
    "draw_battle_result_card(",
    "draw_battle_result_card(106, 10, 202, result, diff, battleId, resultColor)",
    "const char* verdict = diff > 0",
    'CoreS3.Display.printf("%s%s%+ld"',
    "battle_round_title(",
    'out += ",\"clashRoundLabel\":"',
    "const clash=b.clashRound?row(",
    "function diffVerdict(v)",
    "b.powerDiff",
    ".rounds{display:grid",
    "function roundChip(k,v)",
    "function renderRoundChips(b)",
    'out += ",\"battleId\":"',
    'out += ",\"opponentSpecies\":"',
    "const opponent=b.opponentSpecies?",
    "b.opponentElement",
    "b.opponentLevel",
    "opponent:{name:b.opponentSpecies",
    "Number(b.battleId)",
    "id=%08lX",
    'CoreS3.Display.printf("NEXT ',
    'CoreS3.Display.print("NEXT '
)
$MissingBattleRoundEvidence = $BattleRoundEvidence | Where-Object { !$SketchText.Contains($_) }
$BattleResultDoc = $DocText.Contains("settlement verdict card")
if ($MissingBattleRoundEvidence.Count -eq 0 -and $BattleResultDoc) {
    Add-Pass "BATTLE clash/result exposes round FX and mirrored local battle ID without protocol changes"
} else {
    if (!$BattleResultDoc) {
        $MissingBattleRoundEvidence += "settlement verdict card doc"
    }
    Add-Fail ("BATTLE clash/result is missing round FX or mirrored local battle ID evidence: " + ($MissingBattleRoundEvidence -join ", "))
}

$ClashDrawIndex = $SketchText.IndexOf("draw_battle_clash(packet);")
$ClashTimerIndex = $SketchText.IndexOf("battle_clash_started_ms = millis();")
$ClashDueIndex = $SketchText.IndexOf("battle_result_due_ms = battle_clash_started_ms + kBattleClashMs;")
if ($ClashDrawIndex -ge 0 -and $ClashTimerIndex -gt $ClashDrawIndex -and $ClashDueIndex -gt $ClashTimerIndex) {
    Add-Pass "BATTLE clash timer starts after entry drawing and sounds"
} else {
    Add-Fail "BATTLE clash timer must start after entry drawing and sounds"
}

if ($SketchText.Contains("match_sync_step_label(step)") -and
    $SketchText.Contains("battle_peer_id & 0xffffffUL") -and
    $SketchText.Contains("last_peer_seen_ms == 0 ?") -and
    $SketchText.Contains("lastResultColor = app_last_battle_score_diff") -and
    $SketchText.Contains("CoreS3.Display.fillCircle(21, 106, 4, lastResultColor)") -and
    $SketchText.Contains("draw_game_title(match_sync_step_label(match_sync_step())") -and
    $DocText.Contains("device MATCH title follows pairing stage")) {
    Add-Pass "MATCH status panel shows pairing stage, opponent short ID, recent-result cue, and middle-exit hint"
} else {
    Add-Fail "MATCH status panel is missing pairing stage, dynamic title, opponent short ID, recent-result cue, or middle-exit hint"
}

$RequiredDocMarkers = @(
    "## Complete Player Flow",
    "## Screen Information and Buttons",
    "## Button Text and Actions",
    "## Battle Phase Design",
    "## Battle Settlement Rules",
    "## Growth and Reward Rules",
    "## Sound Trigger Points",
    "## Local Friendship Mechanism",
    "kBagVersion",
    "BattlePetPacket v2"
)
$MissingDocMarkers = $RequiredDocMarkers | Where-Object { !$DocText.Contains($_) }
if ($MissingDocMarkers.Count -eq 0) {
    Add-Pass "player-flow-ui.md contains required Module 3 output sections"
} else {
    Add-Fail ("player-flow-ui.md is missing sections: " + ($MissingDocMarkers -join ", "))
}

$DeviceAcceptanceMarkers = @(
    "# Device Acceptance Checklist",
    "## Preflight",
    "## Single-Board Player Flow",
    "## Two-Board Battle Flow",
    "## SD Audio Payload",
    "## App and Serial Control",
    "## Pass Criteria",
    "IDLE -> PHOTO -> WILD/CAPTURE_FAIL",
    "BAG -> SELECT -> IDLE -> MATCH",
    "MATCH -> BATTLE ENTRY -> CLASH -> RESULT -> EXIT",
    "SOUND idle|photo|wild|bag|capture|release|select|match|clash|friend|win|draw|lose|warning|intro|level|exit|cancel",
    "ACT friend"
)
$MissingDeviceAcceptanceMarkers = $DeviceAcceptanceMarkers | Where-Object { !$DeviceAcceptanceText.Contains($_) }
if ($MissingDeviceAcceptanceMarkers.Count -eq 0) {
    Add-Pass "device-acceptance.md covers physical touch, battle, SD audio, App, and serial validation"
} else {
    Add-Fail ("device-acceptance.md is missing markers: " + ($MissingDeviceAcceptanceMarkers -join ", "))
}

if (!$SkipGit -and (Get-Command git -ErrorAction SilentlyContinue)) {
    $PublicHeaders = @(
        "arduino_demos/04_camera_pet_battle/pet_model.h",
        "arduino_demos/04_camera_pet_battle/vision_types.h",
        "arduino_demos/04_camera_pet_battle/ui_types.h",
        "arduino_demos/04_camera_pet_battle/battle_protocol.h"
    )
    $ChangedHeaders = & git diff --name-only -- $PublicHeaders
    if ($LASTEXITCODE -ne 0) {
        Add-Fail "git diff failed while checking public headers"
    } elseif ($ChangedHeaders.Count -eq 0) {
        Add-Pass "public headers are unchanged in git diff"
    } else {
        Add-Fail ("public headers have local changes: " + ($ChangedHeaders -join ", "))
    }
}

if ($Failures.Count -gt 0) {
    Write-Host ""
    Write-Host "Module 3 static acceptance failed:"
    $Failures | ForEach-Object { Write-Host " - $_" }
    exit 1
}

Write-Host ""
Write-Host "Module 3 static acceptance passed."
