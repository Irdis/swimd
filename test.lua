Swimd = require("swimd")

print(Swimd.say_hello("abc1"))
Swimd.init()
Swimd.setup_workspace("c:\\Projects")
print(vim.inspect(Swimd.process_input("cpp", 5)))
-- Swimd.shutdown()

