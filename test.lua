local os_name = vim.loop.os_uname().sysname

if os_name == "Windows_NT" then
    package.loadlib('./build/git2.dll', '')
    package.cpath = './build/?.dll;' .. package.cpath
elseif os_name == "Linux" then
    package.loadlib('./build/libgit2.so', '')
    package.cpath = './build/?.so;' .. package.cpath
else
    print("Running on unknown OS: " .. os_name)
end

Swimd = require("swimd")
print(Swimd.say_hello("abc1"))
Swimd.init()
Swimd.setup_workspace("/home/ivan/Projects/linux")
Swimd.refresh_workspace()
print(vim.inspect(Swimd.process_input("main", 5, Swimd.SCANNER_GIT)))
Swimd.shutdown()

