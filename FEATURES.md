# Eternal Compositor - Feature List (100+ Features)

**Hybrid Scrollable-Tiling Wayland Compositor combining Niri + Hyprland**

## Core Window Management (Features 1-15)
1. Scrollable tiling layout - infinite horizontal strip (from Niri)
2. Dwindle spiral tiling layout (from Hyprland)
3. Master-stack tiling layout with configurable master area
4. Monocle/fullscreen single-window layout
5. Floating window layout with smart placement
6. Grid-based automatic tiling layout
7. Fibonacci spiral tiling layout
8. Fixed columns tiling layout
9. Per-workspace layout override
10. Per-monitor layout override
11. Runtime layout switching (cycle between all 8)
12. Window grouping/tabbing (combine windows into tabbed groups)
13. Pseudo-tiling (window chooses size within tile)
14. Split direction control (horizontal/vertical/auto)
15. Smart split based on window dimensions

## Scrollable Layout Features (16-28) - From Niri
16. Infinite horizontal scrolling viewport
17. Column-based window arrangement
18. Multiple windows stacked vertically per column
19. Opening new windows never resizes existing ones
20. Configurable default column width (proportion or fixed pixels)
21. Preset column width cycling
22. Center-focused-column mode (never/always/on-overflow)
23. Smooth spring-physics scrolling
24. Scroll-to-window navigation
25. Column width adjustment (grow/shrink)
26. Per-column display mode (normal/tabbed)
27. Viewport struts (reserved screen edges)
28. Focus ring with active/inactive colors

## Workspace System (29-42) - Hybrid
29. Dynamic workspace creation (always one empty at bottom)
30. Dynamic workspace deletion (auto-remove empty)
31. Named workspaces
32. Numbered workspaces
33. Per-monitor independent workspace stacks (from Niri)
34. Special/scratchpad workspace (from Hyprland)
35. Persistent workspaces (survive being empty)
36. Workspace groups
37. Move window to workspace (follow focus)
38. Move window to workspace silently (don't follow)
39. Swap workspaces
40. Move workspace to different monitor
41. Workspace rules (per-workspace settings)
42. Workspace overview/exposé mode

## Animation System (43-55)
43. Window open/close animations (slide, fade, zoom)
44. Window move animations with easing
45. Window resize crossfade animations
46. Workspace switch slide animations
47. Fade-in/fade-out transitions
48. Custom bezier curve definitions
49. Spring physics animations (stiffness, damping, mass)
50. Animation timeline chaining (sequential + parallel)
51. Border color transition animations
52. Shadow transition animations
53. Dim transition animations
54. Per-animation-type curve and duration override
55. Global animation speed multiplier

## Decoration & Visual Effects (56-72)
56. Rounded corners with per-corner radius control
57. Window shadows with configurable range, color, offset
58. Dual-Kawase background blur
59. Blur noise, contrast, brightness, vibrancy controls
60. Active/inactive window opacity
61. Dim inactive windows
62. Gradient borders (linear gradient with angle)
63. Oklab/Oklch color space for gradients
64. Active/inactive border colors
65. Configurable border width
66. Custom fragment shader support (screen shaders)
67. Window-specific decoration overrides via rules
68. Popup/layer blur support
69. Anti-aliased rounded corner rendering
70. Per-window opacity control
71. Fullscreen opacity override
72. Shadow scale and sharpness control

## Input Handling (73-90)
73. Full xkbcommon keyboard support (layouts, variants, options)
74. Key repeat rate and delay configuration
75. NumLock/CapsLock default state
76. Per-device input configuration
77. Mouse acceleration profiles (flat, adaptive)
78. Mouse sensitivity adjustment
79. Natural scroll toggle
80. Touchpad tap-to-click
81. Touchpad disable-while-typing
82. Touchpad drag and drag-lock
83. Scroll factor per device
84. Drawing tablet support with output mapping
85. Tablet area mapping and rotation
86. Touch screen support
87. 3-finger swipe gestures (workspace switch)
88. 4-finger swipe gestures (configurable)
89. Pinch gestures (zoom, overview)
90. Hold gestures (toggle floating)

## Keybinding System (91-100)
91. Modifier key combinations (super, alt, shift, ctrl)
92. Mouse button bindings
93. Mouse drag bindings (move/resize)
94. Scroll wheel bindings
95. Submaps/modes (e.g., resize mode)
96. Per-key repeat control
97. Key release bindings
98. Locked bindings (work on lockscreen)
99. Transparent bindings (pass through)
100. Bind flags (repeat, release, locked)

## IPC & Dispatchers (101-130)
101. Unix socket IPC server
102. JSON response format
103. `exec` - launch applications
104. `exec_once` - launch on startup only
105. `killactive` - close focused window
106. `closewindow` - close specific window
107. `movewindow` - move window directionally
108. `resizewindow` - resize window by delta
109. `fullscreen` - toggle fullscreen (real + maximize)
110. `togglefloating` - float/tile toggle
111. `pin` - pin window to all workspaces
112. `focuswindow` - directional focus
113. `swapwindow` - swap with adjacent
114. `centerwindow` - center floating window
115. `setopacity` - change window opacity
116. `workspace` - switch workspace
117. `movetoworkspace` - send window to workspace
118. `movetoworkspacesilent` - send without following
119. `togglespecialworkspace` - scratchpad toggle
120. `focusmonitor` - focus different monitor
121. `movecurrentworkspacetomonitor` - move workspace
122. `swapactiveworkspaces` - swap between monitors
123. `togglesplit` - change split direction
124. `cyclelayout` - switch layout engine
125. `togglegroup` - create/dissolve tab group
126. `changegroupactive` - cycle tabs
127. `lockactivegroup` - prevent group changes
128. `scrollleft/scrollright` - scroll viewport
129. `centercolumn` - center focused column
130. `setcolumnwidth` - adjust column width

## Additional Dispatchers (131-145)
131. `toggleoverview` - workspace overview
132. `zoomin/zoomout` - viewport zoom
133. `screenshot` - capture screen/region/window
134. `screenrecord` - start/stop recording
135. `exit` - quit compositor
136. `reload` - reload configuration
137. `forceidle` - force idle state
138. `setcursor` - change cursor theme
139. `switchxkblayout` - change keyboard layout
140. `dpms` - turn monitors on/off
141. `submap` - enter/exit binding mode
142. `swapwithmaster` - swap with master window
143. `addmaster/removemaster` - adjust master count
144. `scrollcolumn` - scroll to first/last column
145. `scrolltowindow` - scroll to specific window

## Window Rules (146-158)
146. Match by app-id (class)
147. Match by title (regex support)
148. Match by floating state
149. Match by fullscreen state
150. Match by workspace
151. Match by monitor/output
152. Match by XWayland state
153. Rule: force float/tile
154. Rule: set opacity
155. Rule: set workspace
156. Rule: set size and position
157. Rule: disable blur/shadow/border/rounding
158. Rule: block from screencasts

## Monitor Management (159-168)
159. Multi-monitor support
160. Per-monitor resolution and refresh rate
161. Monitor position/arrangement
162. Per-monitor scale (fractional scaling)
163. Monitor transform/rotation
164. VRR (Variable Refresh Rate) / Adaptive Sync
165. DPMS control (screen on/off)
166. Monitor hotplug handling
167. Preferred mode auto-detection
168. Multi-GPU / hybrid GPU support

## Screenshot & Recording (169-174)
169. Full output screenshot
170. Region selection screenshot
171. Window screenshot
172. Screenshot to clipboard
173. Screen recording via PipeWire
174. Sensitive window blocking in recordings

## System Integration (175-185)
175. XWayland support for X11 apps
176. xdg-desktop-portal integration
177. Layer-shell protocol (bars, launchers, notifications)
178. Gamma control protocol
179. Screencopy protocol
180. Idle inhibit protocol
181. Foreign toplevel management
182. Primary selection / clipboard
183. Drag and drop
184. Environment variable configuration
185. Autostart/exec-once applications

## Plugin System (186-192)
186. Dynamic plugin loading (dlopen/dlclose)
187. Plugin API for custom dispatchers
188. Plugin API for custom decorations
189. Plugin API for custom layouts
190. Plugin hooks (window create/destroy, workspace change, render)
191. Plugin configuration in main config
192. Hot-reload plugins

## Configuration (193-200)
193. KDL configuration format
194. Live config reload (inotify)
195. Invalid config doesn't crash (preserves last working state)
196. Per-device input config overrides
197. Custom bezier curve definitions
198. Submap/mode support
199. Conditional config sections
200. Config error notifications

## eternalctl CLI Tool (201-210)
201. `eternalctl dispatch` - execute dispatcher
202. `eternalctl monitors` - list monitors (JSON)
203. `eternalctl workspaces` - list workspaces (JSON)
204. `eternalctl windows` - list windows (JSON)
205. `eternalctl activewindow` - get focused window
206. `eternalctl layers` - list layer surfaces
207. `eternalctl version` - show version
208. `eternalctl reload` - reload config
209. `eternalctl kill` - kill compositor
210. `eternalctl getoption/setoption` - runtime config
