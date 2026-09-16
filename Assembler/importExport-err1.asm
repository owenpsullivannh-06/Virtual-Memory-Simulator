#
# tests import and export durectives with illegal operands
# also tests multiple references to import, export, definition

import
import r0
import r0,10
import x
export
export r0,r0
export r0,8(r0)
export r0,r0
export x

x:

