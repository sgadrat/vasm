; (C)2020 Jannik Vogel, original homebrew
; (C)2022 Sylvain Gadrat, porting to ASM

; Be a batman board because it has a simple GPIO thingy
.dsb 0, $5ce1
.word $42c2
.word $5e42

.dsb 0, $6000-*

.label BATMAN_INPUT_GPIO, $3d01
.label BATMAN_INPUT_GPIO_UP, $8000
.label BATMAN_INPUT_GPIO_DOWN, $4000
.label BATMAN_INPUT_GPIO_LEFT, $2000
.label BATMAN_INPUT_GPIO_RIGHT, $1000

; Main loop
set r3, 0
set r4, 0

start:

.label move_speed, 1

load r1, BATMAN_INPUT_GPIO
set r2, BATMAN_INPUT_GPIO_LEFT
test r1, r2
jne skip_left
	set r1, move_speed
	add r3, r1
skip_left:

load r1, BATMAN_INPUT_GPIO
set r2, BATMAN_INPUT_GPIO_RIGHT
test r1, r2
jne skip_right
	set r1, move_speed
	sub r3, r1
skip_right:

load r1, BATMAN_INPUT_GPIO
set r2 BATMAN_INPUT_GPIO_UP
test r1, r2

; Decide which color to use for the palette
jne set_blue
	goto fill_red
set_blue:
	goto fill_blue

; Fill palette
fill_red:
	.repeat $100, i
		set r1, ((i >> 3) << 10) + (0 << 5) + 0 ; color = arrr rrgg gggb bbbb - a: ALPHA (0:oopaque/1:transparent) - rrrrr: red - ggggg: green - bbbbb: blue
		store $2b00+i, r1
	.end_repeat
	goto fill_done
fill_blue:
	.repeat $100, i
		set r1, ((0 >> 3) << 10) + (0 << 5) + i ; color = arrr rrgg gggb bbbb - a: ALPHA (0:oopaque/1:transparent) - rrrrr: red - ggggg: green - bbbbb: blue
		store $2b00+i, r1
	.end_repeat
fill_done:

; Prepare some sprite table
.label sprite_base, $0000
set r1, sprite_base/$40
store $2822, r1

; Create sprites
;TODO This a full $ff sprite, make an equivalent of the original rom
.label cx, 4
.label cy, 4
.repeat cy, y
	.repeat cx, x
		.redef sprite_i, y * cx + x
		.redef tile, 1 + sprite_i
		.repeat 32, dy
			.repeat 16, dx
				.redef sprite_xy, dy * 16 + dx
				set r1, $ffff
				store sprite_base + tile * sprite_size + sprite_xy, r1
			.end_repeat
		.end_repeat
	.end_repeat
.end_repeat

; Place sprites
.repeat cy, y
	.repeat cx, x
		.redef sprite_i, y * cx + x
		.redef tile, 1 + sprite_i
		.redef pos_x, x*32
		.redef pos_y, 50-y*32
		.redef depth, 1
		.redef w, SS_32
		.redef h, SS_32
		.redef nc, SC_8

		.redef attr, (depth << 12) | (w << 6) | (h << 4) | nc

		set r1, tile
		store $2c00+sprite_i*4+0, r1

		set r1, pos_x
		add r1, r3
		store $2c00+sprite_i*4+1, r1

		set r1, pos_y
		add r1, r4
		store $2c00+sprite_i*4+2, r1

		set r1, attr
		store $2c00+i*4+3, r1
	.end_repeat
.end_repead

; Enable sprites
set r1, $0001
store $2842, r1

goto start

.dsb 0, $fff7-*
.word $6000

.dsb 0, $10000-*
