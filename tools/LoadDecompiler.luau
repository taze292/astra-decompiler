-- Settings
-- Update this URL if the repository owner, repository name, or default branch changes.
local SourceUrl = "https://raw.githubusercontent.com/taze292/astra-decompiler/main/tools/Decompile.luau"

-- Functions
local function LoadDecompiler()
    local Downloaded, Source = pcall(game.HttpGet, game, SourceUrl)
    if not Downloaded or type(Source) ~= "string" then
        return nil
    end

    local Chunk = loadstring(Source)
    if type(Chunk) ~= "function" then
        return nil
    end

    local Loaded, Bridge = pcall(Chunk)
    return if Loaded then Bridge else nil
end

-- Installation
return LoadDecompiler()
