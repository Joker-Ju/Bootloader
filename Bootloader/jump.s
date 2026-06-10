				AREA    |.text|, CODE, READONLY

jump_to_app     PROC
				EXPORT  jump_to_app

				CPSID   I                    ; 1. 关全局中断

				LDR     R1, [R0, #0]         ; 2. R1 = App 向量表[0] = App 栈顶
				MSR     MSP, R1              ; 3. 设 MSP = App 栈顶

				LDR     R1, =0xE000ED08      ; VTOR 地址
				STR     R0, [R1]             ; VTOR = app_addr（R0 就是传进来的地址）
				
				LDR     R1, [R0, #4]         ; 4. R1 = App 向量表[1] = App 入口
				BX      R1                   ; 5. 跳过去，永远不回来

				ENDP
				END