[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Executable,
    [Parameter(Mandatory)][string]$Game,
    [ValidateSet('DLSS','FSR','RR')][string]$Backend='DLSS',
    [string]$Terrain
)
$ErrorActionPreference='Stop'
$program=(Resolve-Path -LiteralPath $Executable).Path
$gamePath=(Resolve-Path -LiteralPath $Game).Path
if($Terrain){$env:ARCEUS_TERRAIN_DIR=(Resolve-Path -LiteralPath $Terrain).Path}
$env:ARCEUS_UPSCALER=if($Backend -eq 'FSR'){'FSR'}else{'DLSS'}
$env:ARCEUS_NGX=if($Backend -eq 'FSR'){'0'}else{'1'}
$env:ARCEUS_DLSS_RR=if($Backend -eq 'RR'){'1'}else{'0'}
$env:ARCEUS_PT_FILTER='1'
$env:SNX_ARCEUS_FIXED_RESOLUTION='1'
$env:SNX_ARCEUS_SCREEN_FETCH='1'
$env:SNX_ARCEUS_NATIVE_SHADOWS='1'
$env:SNX_ARCEUS_NEUTRAL_RIM='1'
$env:SNX_ARCEUS_FPS60='0'
$env:SNX_ARCEUS_TARGET_FPS='120'
$env:SNX_ARCEUS_CAMERA_SPEED='2.2'
$env:SNX_ARCEUS_TIMING_FIXES='1'
$env:SNX_ARCEUS_MENU_REPEAT_FIX='1'
$env:SNX_ARCEUS_EVENT_CAMERA_FIX='1'
$env:SNX_ARCEUS_CAMERA_INPUT_FIX='1'
$env:SNX_ARCEUS_BLINK_FIX='1'
$env:SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS='1'
Push-Location -LiteralPath $gamePath
try {
    & $program "--game=$gamePath" --nvn_native=0 --native_bc=1 --fps_limit=game --fullscreen=1 --log=1 --seconds=0 --gamepad_background=1 --docked=1 --resolution=2.0
    exit $LASTEXITCODE
} finally {Pop-Location}
