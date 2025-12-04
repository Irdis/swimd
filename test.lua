Swimd = require("swimd")

print(Swimd.say_hello("abc1"))
Swimd.init()
Swimd.setup_workspace("c:\\projects")
print(vim.inspect(Swimd.process_input("main", 5)))
Swimd.shutdown()

