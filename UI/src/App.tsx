import { For, Show, createEffect, createMemo, createSignal } from "solid-js";
import "./app.css";
import { Index } from "solid-js";
type Color = [number, number, number, number];
type AttackWarning = {
  id: number;
  direction: number;
  xPercent: number;
  yPercent: number;
  resolutionScale: number;
  distanceScale: number;
  hitFrameFlash: boolean;
  unblockable: boolean;
};
type NpcUiConfig = {
  enabled: boolean;
  // NPC center piece has its own settings, independent of player HUD.
  showCenter: boolean;
  centerPieceSizePercent: number;
  centerColor: Color;
  centerImage: string;
  scaleWithResolution: boolean;
  scaleWithDistance: boolean;
  mirrorAttackDirections: boolean;
  scalePercent: number;
  opacityPercent: number;
  activeOpacityPercent: number;
  offsetXPixels: number;
  offsetYPixels: number;
  rotationDegrees: number;
  diameterPixels: number;
  activeColor: Color;
  unblockableColor: Color;
  inactiveColor: Color;
  borderColor: Color;
  sharedSegmentImage: string;
  segmentImages: string[];
};
type UiConfig = {
  enabled: boolean;
  editMode: boolean;
  showCenter: boolean;
  moveCenterWithDirection: boolean;
  centerInThirdPerson: boolean;
  scaleWithResolution: boolean;
  positionXPercent: number;
  positionYPercent: number;
  attachOffsetXPixels: number;
  attachOffsetYPixels: number;
  scalePercent: number;
  diameterPixels: number;
  centerPieceSizePercent: number;
  rotationDegrees: number;
  opacityPercent: number;
  activeOpacityPercent: number;
  centerMovementPixels: number;
  centerMovementDurationMs: number;
  activeColor: Color;
  unblockableColor: Color;
  inactiveColor: Color;
  borderColor: Color;
  centerColor: Color;
  centerActiveColor: Color;
  sharedSegmentImage: string;
  segmentImages: string[];
  centerImage: string;
  npc: NpcUiConfig;
};
const defaultNpcConfig = (): NpcUiConfig => ({
  enabled: true,
  showCenter: true,
  centerPieceSizePercent: 100,
  centerColor: [0.04, 0.04, 0.05, 0.86],
  centerImage: "",
  scaleWithResolution: true,
  scaleWithDistance: true,
  mirrorAttackDirections: true,
  scalePercent: 52,
  opacityPercent: 100,
  activeOpacityPercent: 100,
  offsetXPixels: 0,
  offsetYPixels: 0,
  rotationDegrees: 0,
  diameterPixels: 260,
  activeColor: [0.95, 0.72, 0.22, 1],
  unblockableColor: [1.0, 0.2, 0.1, 1],
  inactiveColor: [0.08, 0.09, 0.1, 0.72],
  borderColor: [0.8, 0.8, 0.76, 0.9],
  sharedSegmentImage: "",
  segmentImages: Array.from({ length: 3 }, () => ""),
});
const defaultConfig = (): UiConfig => ({
  enabled: true,
  editMode: false,
  showCenter: true,
  moveCenterWithDirection: false,
  centerInThirdPerson: false,
  scaleWithResolution: true,
  positionXPercent: 50,
  positionYPercent: 50,
  attachOffsetXPixels: 0,
  attachOffsetYPixels: 0,
  scalePercent: 100,
  diameterPixels: 260,
  centerPieceSizePercent: 100,
  rotationDegrees: 0,
  opacityPercent: 100,
  activeOpacityPercent: 100,
  centerMovementPixels: 20,
  centerMovementDurationMs: 100,
  activeColor: [0.95, 0.72, 0.22, 1],
  unblockableColor: [1.0, 0.2, 0.1, 1],
  inactiveColor: [0.08, 0.09, 0.1, 0.72],
  borderColor: [0.8, 0.8, 0.76, 0.9],
  centerColor: [0.04, 0.04, 0.05, 0.86],
  centerActiveColor: [0.95, 0.72, 0.22, 1],
  sharedSegmentImage: "",
  segmentImages: Array.from({ length: 3 }, () => ""),
  centerImage: "",
  npc: defaultNpcConfig(),
});
const clamp = (value: number, minimum: number, maximum: number) =>
  Math.min(
    maximum,
    Math.max(minimum, Number.isFinite(value) ? value : minimum),
  );
const parseColor = (value: unknown, fallback: Color): Color => {
  if (!Array.isArray(value) || value.length < 3) return fallback;
  return [
    clamp(Number(value[0]), 0, 1),
    clamp(Number(value[1]), 0, 1),
    clamp(Number(value[2]), 0, 1),
    clamp(Number(value[3] ?? 1), 0, 1),
  ];
};
const safeAssetPath = (value: unknown) => {
  if (typeof value !== "string") return "";
  const path = value.replaceAll("\\\\\\\\\\\\\\\\", "/").trim();
  if (
    !path ||
    path.includes("..") ||
    path.includes(":") ||
    path.startsWith("/") ||
    path.startsWith("//")
  ) {
    return "";
  }
  return path.startsWith("./") ? path : `./${path}`;
};
const parseConfig = (payload: string): UiConfig | null => {
  try {
    const value = JSON.parse(payload) as Partial<UiConfig>;
    const fallback = defaultConfig();
    const npc: Partial<NpcUiConfig> = value.npc ?? {};
    const npcFallback = fallback.npc;
    return {
      enabled: value.enabled !== false,
      editMode: value.editMode === true,
      showCenter: value.showCenter !== false,
      moveCenterWithDirection: value.moveCenterWithDirection === true,
      centerInThirdPerson: value.centerInThirdPerson === true,
      scaleWithResolution: value.scaleWithResolution !== false,
      positionXPercent: clamp(
        Number(value.positionXPercent ?? fallback.positionXPercent),
        0,
        100,
      ),
      positionYPercent: clamp(
        Number(value.positionYPercent ?? fallback.positionYPercent),
        0,
        100,
      ),
      attachOffsetXPixels: clamp(
        Number(value.attachOffsetXPixels ?? 0),
        -1000,
        1000,
      ),
      attachOffsetYPixels: clamp(
        Number(value.attachOffsetYPixels ?? 0),
        -1000,
        1000,
      ),
      scalePercent: clamp(
        Number(value.scalePercent ?? fallback.scalePercent),
        25,
        300,
      ),
      diameterPixels: clamp(
        Number(value.diameterPixels ?? fallback.diameterPixels),
        96,
        800,
      ),
      centerPieceSizePercent: clamp(
        Number(value.centerPieceSizePercent ?? fallback.centerPieceSizePercent),
        10,
        200,
      ),
      rotationDegrees: clamp(
        Number(value.rotationDegrees ?? fallback.rotationDegrees),
        -180,
        180,
      ),
      opacityPercent: clamp(
        Number(value.opacityPercent ?? fallback.opacityPercent),
        0,
        100,
      ),
      activeOpacityPercent: clamp(
        Number(value.activeOpacityPercent ?? fallback.activeOpacityPercent),
        0,
        100,
      ),
      centerMovementPixels: clamp(
        Number(value.centerMovementPixels ?? fallback.centerMovementPixels),
        0,
        400,
      ),
      centerMovementDurationMs: clamp(
        Number(
          value.centerMovementDurationMs ?? fallback.centerMovementDurationMs,
        ),
        0,
        1000,
      ),
      activeColor: parseColor(value.activeColor, fallback.activeColor),
      unblockableColor: parseColor(
        value.unblockableColor,
        fallback.unblockableColor,
      ),
      inactiveColor: parseColor(value.inactiveColor, fallback.inactiveColor),
      borderColor: parseColor(value.borderColor, fallback.borderColor),
      centerColor: parseColor(value.centerColor, fallback.centerColor),
      centerActiveColor: parseColor(value.centerActiveColor, fallback.centerActiveColor),
      sharedSegmentImage: safeAssetPath(value.sharedSegmentImage),
      segmentImages: Array.from({ length: 3 }, (_, index) =>
        safeAssetPath(
          Array.isArray(value.segmentImages) ? value.segmentImages[index] : "",
        ),
      ),
      centerImage: safeAssetPath(value.centerImage),
      npc: {
        enabled: npc.enabled !== false,
        showCenter: npc.showCenter !== false,
        centerPieceSizePercent: clamp(Number(npc.centerPieceSizePercent ?? npcFallback.centerPieceSizePercent), 10, 200),
        centerColor: parseColor(npc.centerColor, npcFallback.centerColor),
        centerImage: safeAssetPath(npc.centerImage),
        scaleWithResolution: npc.scaleWithResolution !== false,
        scaleWithDistance: npc.scaleWithDistance !== false,
        mirrorAttackDirections: npc.mirrorAttackDirections !== false,
        scalePercent: clamp(
          Number(npc.scalePercent ?? npcFallback.scalePercent),
          20,
          200,
        ),
        opacityPercent: clamp(
          Number(npc.opacityPercent ?? npcFallback.opacityPercent),
          0,
          100,
        ),
        activeOpacityPercent: clamp(
          Number(npc.activeOpacityPercent ?? npcFallback.activeOpacityPercent),
          0,
          100,
        ),
        offsetXPixels: clamp(
          Number(npc.offsetXPixels ?? npcFallback.offsetXPixels),
          -1000,
          1000,
        ),
        offsetYPixels: clamp(
          Number(npc.offsetYPixels ?? npcFallback.offsetYPixels),
          -1000,
          1000,
        ),
        rotationDegrees: clamp(
          Number(npc.rotationDegrees ?? npcFallback.rotationDegrees),
          -180,
          180,
        ),
        diameterPixels: clamp(
          Number(npc.diameterPixels ?? npcFallback.diameterPixels),
          96,
          800,
        ),
        activeColor: parseColor(npc.activeColor, npcFallback.activeColor),
        unblockableColor: parseColor(
          npc.unblockableColor,
          npcFallback.unblockableColor,
        ),
        inactiveColor: parseColor(npc.inactiveColor, npcFallback.inactiveColor),
        borderColor: parseColor(npc.borderColor, npcFallback.borderColor),
        sharedSegmentImage: safeAssetPath(npc.sharedSegmentImage),
        segmentImages: Array.from({ length: 3 }, (_, index) =>
          safeAssetPath(
            Array.isArray(npc.segmentImages) ? npc.segmentImages[index] : "",
          ),
        ),
      },
    };
  } catch {
    return null;
  }
};
const parseAttackWarnings = (payload: string): AttackWarning[] => {
  try {
    const value = JSON.parse(payload) as unknown;
    if (!Array.isArray(value)) return [];
    return value.slice(0, 32).flatMap((item) => {
      if (!item || typeof item !== "object") return [];
      const candidate = item as Partial<AttackWarning>;
      const id = Math.trunc(Number(candidate.id));
      const direction = Math.trunc(Number(candidate.direction));
      if (!Number.isFinite(id) || id < 0 || direction < 0 || direction > 3) {
        return [];
      }
      return [
        {
          id,
          direction,
          xPercent: clamp(Number(candidate.xPercent), 0, 100),
          yPercent: clamp(Number(candidate.yPercent), 0, 100),
          resolutionScale: clamp(Number(candidate.resolutionScale), 0.5, 4),
          distanceScale: clamp(Number(candidate.distanceScale ?? 1), 0.35, 1),
          hitFrameFlash: candidate.hitFrameFlash === true,
          unblockable: candidate.unblockable === true,
        },
      ];
    });
  } catch {
    return [];
  }
};
let settingsReceiver: ((payload: string) => void) | null = null;
let directionReceiver: ((payload: string) => void) | null = null;
let runtimeReceiver: ((payload: string) => void) | null = null;
let attackWarningsReceiver: ((payload: string) => void) | null = null;
let pendingSettings = "";
let pendingDirection = "0";
let pendingRuntime = "0|0|50|50|1|0";
let pendingAttackWarnings = "[]";
declare global {
  interface Window {
    updateForHonorCombatSettings: (payload: string) => void;
    updateForHonorCombatDirection: (payload: string) => void;
    updateForHonorCombatRuntime: (payload: string) => void;
    updateForHonorCombatAttackWarnings: (payload: string) => void;
  }
}
window.updateForHonorCombatSettings = (payload: string) => {
  if (settingsReceiver) settingsReceiver(payload);
  else pendingSettings = payload;
};
window.updateForHonorCombatDirection = (payload: string) => {
  if (directionReceiver) directionReceiver(payload);
  else pendingDirection = payload;
};
window.updateForHonorCombatRuntime = (payload: string) => {
  if (runtimeReceiver) runtimeReceiver(payload);
  else pendingRuntime = payload;
};
window.updateForHonorCombatAttackWarnings = (payload: string) => {
  if (attackWarningsReceiver) attackWarningsReceiver(payload);
  else pendingAttackWarnings = payload;
};
const colorToCss = (color: Color) => {
  const red = Math.round(clamp(color[0], 0, 1) * 255);
  const green = Math.round(clamp(color[1], 0, 1) * 255);
  const blue = Math.round(clamp(color[2], 0, 1) * 255);
  return `rgba(${red}, ${green}, ${blue}, ${clamp(color[3], 0, 1)})`;
};
const supportedExtensions = ["svg", "png", "webp", "jpg", "jpeg", "gif"];
const loadFirstAsset = (
  candidates: string[],
  generation: number,
  currentGeneration: () => number,
) =>
  new Promise<string>((resolve) => {
    let index = 0;
    const tryNext = () => {
      if (generation !== currentGeneration() || index >= candidates.length) {
        resolve("");
        return;
      }
      const candidate = candidates[index++];
      const image = new Image();
      image.onload = () =>
        resolve(generation === currentGeneration() ? candidate : "");
      image.onerror = tryNext;
      image.src = candidate;
    };
    tryNext();
  });
const guardPaths = [
  // Top
  `M110 8 Q114 10 120 14 L154 39 Q174 53 191 67 L198 73
   Q200 76 196 79 L187 81 Q179 82 173 77 L141 52
   Q125 39 110 29 Q95 39 79 52 L47 77 Q41 82 33 81
   L24 79 Q20 76 22 73 L29 67 Q46 53 66 39
   L100 14 Q106 10 110 8 Z`,
  // Right
  `M194 87 Q189 84 182 85 L171 87 Q166 90 162 97
   L143 124 Q128 146 118 166 L114 177 L114 199
   Q114 204 118 201 L126 190 Q138 173 151 153
   L172 122 Q185 104 193 94 Q196 90 194 87 Z`,
  // Left
  `M26 87 Q31 84 38 85 L49 87 Q54 90 58 97
   L77 124 Q92 146 102 166 L106 177 L106 199
   Q106 204 102 201 L94 190 Q82 173 69 153
   L48 122 Q35 104 27 94 Q24 90 26 87 Z`,
] as const;
// Visual overlay only. These paths follow only the OUTER sides of the
// existing three shield pieces; guardPaths and image clips are untouched.
const guardOuterEdgePaths = [
  // Top: from the left outside tip across the top crest to the right tip.
  `M22 73 L29 67 Q46 53 66 39 L100 14 Q106 10 110 8
   Q114 10 120 14 L154 39 Q174 53 191 67 L198 73`,
  // Right: outside face from the upper tip to the lower point.
  `M194 87 Q196 90 193 94 Q185 104 172 122 L151 153
   Q138 173 126 190 L118 201 Q114 204 114 199`,
  // Left: mirrored outside face from the upper tip to the lower point.
  `M26 87 Q24 90 27 94 Q35 104 48 122 L69 153
   Q82 173 94 190 L102 201 Q106 204 106 199`,
] as const;

const playerVisualDirection = (direction: number) => {
switch (direction) {
    case 1:
      return 0; // Top
    case 2:
      return 1; // Right
    case 3:
      return 2; // Left
    default:
      return -1; // Neutral
  }
};
// Attack directions are incoming from an NPC, so left/right are mirrored
// by default. The NPC UI setting can disable that mirror independently.
const npcVisualDirection = (rawDirection: number, mirror: boolean) => {
  switch (rawDirection) {
    case 1:
      return 0; // Top
    case 2:
      return mirror ? 2 : 1; // NPC right -> screen left when mirrored
    case 3:
      return mirror ? 1 : 2; // NPC left -> screen right when mirrored
    default:
      return -1; // Neutral
  }
};
// ============================================================================
// SHARED SHIELD GEOMETRY AND RENDERER
// The SAME 3-segment paths and center-piece JSX are used for player and NPCs.
// Only colors, orientation, selected segment, and center settings differ.
// ============================================================================
type ShieldPalette = Pick<UiConfig, "activeColor" | "inactiveColor" | "unblockableColor" | "borderColor">;
type ShieldCenter = {
  visible: boolean;
  radius: number;
  color: Color;
  active: boolean;
  activeColor: Color;
  image: string;
  offsetX: number;
  offsetY: number;
  durationMs: number;
};
type ShieldProps = {
  id: string;
  directionIndex: number;
  allActive: boolean;
  unblockable: boolean;
  rotationDegrees: number;
  palette: ShieldPalette;
  shieldOpacityPercent: number;
  activeOpacityPercent: number;
  segmentAssets: string[];
  center: ShieldCenter;
  // Player-only: draw outer rim accents while the player is blocking.
  blocking?: boolean;
  npc?: boolean;
};
// Blend the existing user-selected color toward white or black for subtle depth.
// Alpha is preserved so the existing RGBA pickers and separate opacity controls work.
function shadeColor(color: Color, amount: number): Color {
  const weight = Math.min(Math.abs(amount), 1);
  const target = amount >= 0 ? 1 : 0;
  return [
    color[0] + (target - color[0]) * weight,
    color[1] + (target - color[1]) * weight,
    color[2] + (target - color[2]) * weight,
    color[3],
  ];
}
function Shield(props: ShieldProps) {
  return (
    <svg class="shield-guard" viewBox="0 0 220 220" role="presentation" aria-hidden="true">
      <defs>
        <For each={[0, 1, 2]}>
          {(index: number) => {
            const active = () => props.allActive || props.unblockable || props.directionIndex === index;
            const color = (): Color => props.unblockable
              ? props.palette.unblockableColor
              : active() ? props.palette.activeColor : props.palette.inactiveColor;
            return (
              <>
                <clipPath id={`${props.id}-segment-${index}`}>
                  <path d={guardPaths[index]} />
                </clipPath>
                <linearGradient id={`${props.id}-segment-fill-${index}`} x1="10%" y1="0%" x2="90%" y2="100%">
                  <stop offset="0%" stop-color={colorToCss(shadeColor(color(), 0.32))} />
                  <stop offset="32%" stop-color={colorToCss(shadeColor(color(), 0.08))} />
                  <stop offset="72%" stop-color={colorToCss(shadeColor(color(), -0.08))} />
                  <stop offset="100%" stop-color={colorToCss(shadeColor(color(), -0.32))} />
                </linearGradient>
              </>
            );
          }}
        </For>
        <clipPath id={`${props.id}-center`}>
          <circle cx="110" cy="95" r={props.center.radius} />
        </clipPath>
        <radialGradient id={`${props.id}-center-fill`} cx="30%" cy="25%" r="85%">
          <stop offset="0%" stop-color={colorToCss(shadeColor(props.center.active ? props.center.activeColor : props.center.color, 0.30))} />
          <stop offset="55%" stop-color={colorToCss(props.center.active ? props.center.activeColor : props.center.color)} />
          <stop offset="100%" stop-color={colorToCss(shadeColor(props.center.active ? props.center.activeColor : props.center.color, -0.30))} />
        </radialGradient>
      </defs>
      <g transform={`rotate(${props.rotationDegrees} 110 110)`}>
        <For each={[0, 1, 2]}>
          {(index: number) => {
            const active = () => props.allActive || props.unblockable || props.directionIndex === index;
            return (
              <g
                classList={{ "direction-shield": true, active: active() }}
                style={{ opacity: `${(active() ? props.activeOpacityPercent : props.shieldOpacityPercent) / 100}` }}
              >
                <path
                  class="direction-segment shield-segment"
                  classList={{ "attack-warning-segment": props.npc === true }}
                  d={guardPaths[index]}
                  fill={`url(#${props.id}-segment-fill-${index})`}
                  stroke={colorToCss(props.palette.borderColor)}
                  stroke-width="1.5"
                  stroke-linejoin="round"
                />
                <Show when={props.segmentAssets[index]}>
                  <image
                    class="direction-image"
                    classList={{ active: active() }}
                    href={props.segmentAssets[index]}
                    x="0" y="0" width="220" height="220"
                    preserveAspectRatio="xMidYMid slice"
                    clip-path={`url(#${props.id}-segment-${index})`}
                  />
                </Show>
              </g>
            );
          }}
        </For>
        {/* Player guard rim: outside edge only; drawn over the existing images.
            No changes to the original paths, clipping or shield shapes. */}
        <Show when={props.blocking === true && !props.npc}>
          <For each={guardOuterEdgePaths}>
            {(edgePath) => (
              <path
                class="player-block-outer-edge"
                d={edgePath}
                fill="none"
                stroke={colorToCss(props.palette.activeColor)}
                stroke-width="2"
                stroke-linecap="round"
                stroke-linejoin="round"
                vector-effect="non-scaling-stroke"
                pointer-events="none"
              />
            )}
          </For>
        </Show>
        <Show when={props.center.visible}>
          <g
            class="center-piece-group"
            style={{
              opacity: `${(props.center.active ? props.activeOpacityPercent : props.shieldOpacityPercent) / 100}`,
              transform: `translate(${props.center.offsetX}px, ${props.center.offsetY}px)`,
              "transition-duration": `${props.center.durationMs}ms`,
            }}
          >
            <circle
              class="center-piece"
              cx="110" cy="95" r={props.center.radius}
              fill={`url(#${props.id}-center-fill)`}
              stroke={colorToCss(props.palette.borderColor)}
              stroke-width="1.5"
            />
            <Show when={props.center.image}>
              <image
                class="center-image"
                href={props.center.image}
                x={110 - props.center.radius}
                y={95 - props.center.radius}
                width={props.center.radius * 2}
                height={props.center.radius * 2}
                preserveAspectRatio="xMidYMid slice"
                clip-path={`url(#${props.id}-center)`}
              />
            </Show>
          </g>
        </Show>
      </g>
    </svg>
  );
}
function App() {
  const [config, setConfig] = createSignal(defaultConfig());
  const [direction, setDirection] = createSignal(0);
  const [allBlock, setAllBlock] = createSignal(false);
  const [blocking, setBlocking] = createSignal(false);
  const [unblockable, setUnblockable] = createSignal(false);
  const [parryFlash, setParryFlash] = createSignal(false);
  let lastParrySuccess = false;
  let parryFlashTimer: ReturnType<typeof setTimeout> | undefined;
  const [runtime, setRuntime] = createSignal({
    eligible: false,
    previewEligible: false,
    xPercent: 50,
    yPercent: 50,
    resolutionScale: 1,
    attached: false,
  });
  const [attackWarnings, setAttackWarnings] = createSignal<AttackWarning[]>([]);
  const [segmentAssets, setSegmentAssets] = createSignal<string[]>(
    Array.from({ length: 3 }, () => ""),
  );
  const [npcSegmentAssets, setNpcSegmentAssets] = createSignal<string[]>(
    Array.from({ length: 3 }, () => ""),
  );
  const [centerAsset, setCenterAsset] = createSignal("");
  const [npcCenterAsset, setNpcCenterAsset] = createSignal("");
  let assetGeneration = 0;
  let lastAssetPaths = "";
  settingsReceiver = (payload) => {
    const parsed = parseConfig(payload);
    if (parsed) setConfig(parsed);
  };
  directionReceiver = (payload) => {
    const [rawDirection, rawAllBlock, rawUnblockable, rawParrySuccess, rawBlocking] =
      payload.split("|");
    setDirection(clamp(parseInt(rawDirection, 10) || 0, 0, 3));
    setAllBlock(rawAllBlock === "1");
    setBlocking(rawBlocking === "1");
    setUnblockable(rawUnblockable === "1");
    const success = rawParrySuccess === "1";
    if (success && !lastParrySuccess) {
      setParryFlash(true);
      if (parryFlashTimer !== undefined) {
        clearTimeout(parryFlashTimer);
      }
      parryFlashTimer = setTimeout(() => {
        setParryFlash(false);
        parryFlashTimer = undefined;
      }, 150);
    }
    lastParrySuccess = success;
  };
  runtimeReceiver = (payload) => {
    const parts = payload.split("|");
    if (parts.length < 6) return;
    setRuntime({
      eligible: parts[0] === "1",
      previewEligible: parts[1] === "1",
      xPercent: clamp(Number(parts[2]), 0, 100),
      yPercent: clamp(Number(parts[3]), 0, 100),
      resolutionScale: clamp(Number(parts[4]), 0.5, 4),
      attached: parts[5] === "1",
    });
  };
  attackWarningsReceiver = (payload) =>
    setAttackWarnings(parseAttackWarnings(payload));
  if (pendingSettings) settingsReceiver(pendingSettings);
  directionReceiver(pendingDirection);
  runtimeReceiver(pendingRuntime);
  attackWarningsReceiver(pendingAttackWarnings);
  createEffect(() => {
    const current = config();
    const paths = JSON.stringify([
      current.sharedSegmentImage, ...current.segmentImages,
      current.npc.sharedSegmentImage, ...current.npc.segmentImages,
      current.centerImage, current.npc.centerImage,
    ]);
    if (paths === lastAssetPaths) return;
    lastAssetPaths = paths;
    const generation = ++assetGeneration;
    const getGeneration = () => assetGeneration;
    const resolveSegment = async (
      source: Pick<NpcUiConfig, "segmentImages" | "sharedSegmentImage">,
      index: number,
    ) => {
      const explicit = source.segmentImages[index];
      const shared = source.sharedSegmentImage;
      const candidates = explicit
        ? [explicit]
        : shared
          ? [shared]
          : [
              ...supportedExtensions.map(
                (extension) =>
                  `./Assets/ForHonorCombat/Default/Segment${index + 1}.${extension}`,
              ),
              ...supportedExtensions.map(
                (extension) =>
                  `./Assets/ForHonorCombat/Default/Segment.${extension}`,
              ),
            ];
      return loadFirstAsset(candidates, generation, getGeneration);
    };
    for (let index = 0; index < 3; index++) {
      void resolveSegment(current, index).then((path) => {
        if (generation === assetGeneration) {
          setSegmentAssets((previous) =>
            previous.map((item, itemIndex) =>
              itemIndex === index ? path : item,
            ),
          );
        }
      });
      void resolveSegment(current.npc, index).then((path) => {
        if (generation === assetGeneration) {
          setNpcSegmentAssets((previous) =>
            previous.map((item, itemIndex) =>
              itemIndex === index ? path : item,
            ),
          );
        }
      });
    }
    const centerCandidates = current.centerImage
      ? [current.centerImage]
      : supportedExtensions.map(
          (extension) => `./Assets/ForHonorCombat/Default/Center.${extension}`,
        );
    void loadFirstAsset(centerCandidates, generation, getGeneration).then(
      (path) => {
        if (generation === assetGeneration) {
          setCenterAsset(path);
        }
      },
    );
    // Load the NPC center image separately. Each HUD has its own asset path.
    const npcCenterCandidates = current.npc.centerImage
      ? [current.npc.centerImage]
      : supportedExtensions.map((extension) => `./Assets/ForHonorCombat/Default/Center.${extension}`);
    void loadFirstAsset(npcCenterCandidates, generation, getGeneration).then((path) => {
      if (generation === assetGeneration) setNpcCenterAsset(path);
    });
  });
  // Keep the original center-size slider, scaled to the new shield dot.
  const guardCenterRadius = createMemo(() =>
    Math.max(0.7, 7 * config().centerPieceSizePercent / 100),
  );
  const centerOffset = createMemo(() => {
    if (!config().moveCenterWithDirection || direction() === 0) {
      return { x: 0, y: 0 };
    }
    const angle =
      direction() === 1
        ? -Math.PI / 2
        : direction() === 2
          ? 0
          : Math.PI;
    const distance = config().centerMovementPixels;
    return {
      x: Math.cos(angle) * distance,
      y: Math.sin(angle) * distance,
    };
  });
  // Only a genuinely displaced center uses the active center color/opacity.
  const centerIsActive = createMemo(() =>
    centerOffset().x !== 0 || centerOffset().y !== 0,
  );
  const horizontalPosition = createMemo(() =>
    runtime().attached ? runtime().xPercent : config().positionXPercent,
  );
  const verticalPosition = createMemo(() =>
    runtime().attached ? runtime().yPercent : config().positionYPercent,
  );
  const effectiveScale = createMemo(
    () => (config().scalePercent / 100) * (config().scaleWithResolution ? runtime().resolutionScale : 1),
  );
  return (
    <>
      <div
        class="honor-hud player-hud"
        classList={{
          "hud-visible": config().enabled &&
            (runtime().eligible || unblockable() || (config().editMode && runtime().previewEligible)),
          "parry-success": parryFlash(),
        }}
        style={{
          left: `${horizontalPosition()}%`,
          top: `${verticalPosition()}%`,
          width: `${config().diameterPixels}px`,
          height: `${config().diameterPixels}px`,
          transform: `translate(-50%, -50%) scale(${effectiveScale()})`,
        }}
      >
        <Shield
          id="fhc-player-shield"
          directionIndex={playerVisualDirection(direction())}
          allActive={allBlock() || parryFlash()}
          blocking={blocking()}
          unblockable={unblockable()}
          rotationDegrees={config().rotationDegrees}
          palette={config()}
          shieldOpacityPercent={config().opacityPercent}
          activeOpacityPercent={config().activeOpacityPercent}
          segmentAssets={segmentAssets()}
          center={{
            visible: config().showCenter,
            radius: guardCenterRadius(),
            color: config().centerColor,
            active: centerIsActive(),
            activeColor: config().centerActiveColor,
            image: centerAsset(),
            offsetX: centerOffset().x * 0.22,
            offsetY: centerOffset().y * 0.22,
            durationMs: config().centerMovementDurationMs,
          }}
        />
      </div>
      {/* =====================================================================
          NPC HUD - uses the identical Shield renderer with independent settings.
          NPC warning lifetime and screen coordinates come from the C++ bridge.
          ===================================================================== */}
      <Show when={config().npc.enabled}>
        <Index each={attackWarnings()}>
          {(warning: () => AttackWarning) => {
            const warningScale = () =>
              (config().npc.scalePercent / 100) *
              (config().npc.scaleWithResolution ? warning().resolutionScale : 1) *
              (config().npc.scaleWithDistance ? warning().distanceScale : 1);
            return (
              <div
                class="honor-hud attack-warning"
                classList={{ "hit-frame-flash": warning().hitFrameFlash }}
                data-threat-id={warning().id}
                style={{
                  left: `calc(${warning().xPercent}% + ${config().npc.offsetXPixels * warning().resolutionScale}px)`,
                  top: `calc(${warning().yPercent}% - ${config().npc.offsetYPixels * warning().resolutionScale}px)`,
                  width: `${config().npc.diameterPixels}px`,
                  height: `${config().npc.diameterPixels}px`,
                  transform: `translate(-50%, -50%) scale(${warningScale()})`,
                }}
              >
                <Shield
                  id={`fhc-attack-warning-${warning().id}`}
                  directionIndex={npcVisualDirection(warning().direction, config().npc.mirrorAttackDirections)}
                  allActive={false}
                  unblockable={warning().unblockable}
                  rotationDegrees={config().npc.rotationDegrees}
                  palette={config().npc}
                  shieldOpacityPercent={config().npc.opacityPercent}
                  activeOpacityPercent={config().npc.activeOpacityPercent}
                  segmentAssets={npcSegmentAssets()}
                  npc
                  center={{
                    visible: config().npc.showCenter,
                    radius: Math.max(0.7, 7 * config().npc.centerPieceSizePercent / 100),
                    color: config().npc.centerColor,
                    active: false,
                    activeColor: config().npc.centerColor,
                    image: npcCenterAsset(),
                    offsetX: 0,
                    offsetY: 0,
                    durationMs: 0,
                  }}
                />
              </div>
            );
          }}
        </Index>
      </Show>
    </>
  );
}
export default App;
