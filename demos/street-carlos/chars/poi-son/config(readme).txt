;---------------------------------------------------------------------------
; Initialize (at the start of the round)
[Statedef 5900]
type = S

[State 5900, 1] ;Clear all int variables
type = VarRangeSet
trigger1 = time = 0
trigger1 = roundsexisted = 0
value = 0

[State 5900, 2] ;Clear all float variables
type = VarRangeSet
trigger1 = time = 0
trigger1 = roundsexisted = 0
fvalue = 0

[State 0, 0]
type = VarSet
trigger1 = 1
fvar(11) = 1;2;

;value = 1 ; Non-bonus mode
;value = 2 ; Bonus mode(Strikers exist always.)

[State 0, 0]
type = LifeSet
trigger1 = time = 0
trigger1 = fvar(11) = 2
value = ceil(Life*0.75)

[State 5900, 3] ;Intro
type = ChangeState
trigger1 = roundsexisted = 0
trigger1 = fvar(11) = [1,2]
value = 190

;++++++++++++++++++++++++++++++
;POIZON-ZAK (from Final Fight)
;++++++++++++++++++++++++++++++

;Move list

;Normal Attacks
;---------------
;a button = Weak
;b button = Midium
;c button = Strong(Un-mounting,sorry)
;z button = No Use
;a+b = Stone Attack(Throw)

;Special Attacks
;---------------
;D,DB,B + (a/b/c) = Somersault Kick

;Hyper Attacks (each uses 1 power bar)
;---------------
;D,DF,F,D,DF,F + (a/b/c) = Roling Dance(Non-bonus mode only.)

;+++++++++++++++++++++++++
;author:ZAKO-1(ZAKOH_ICHI)
;+++++++++++++++++++++++++