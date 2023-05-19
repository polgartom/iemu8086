bits 16

org 0x0100

%define SCREEN_WIDTH 128

start:
mov bx, title_screen
mov cx, cs:[frame_buffer_bytes]
mov di, 0
copy_title_screen_loop:
    mov al, cs:[bx + di]
    mov [di], al
    mov ah, cs:[bx + di+1]
    mov [di+1], ah
    add di, 2
    loop copy_title_screen_loop
jmp start

screen_size dw SCREEN_WIDTH
frame_buffer_index db 0
frame_buffer_bytes dw 128*128

title_screen:
%include "jura.dat"