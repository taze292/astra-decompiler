# Luau

This project uses the bytecode definitions and studies the serializer and VM implementation from [luau-lang/luau](https://github.com/luau-lang/luau). The Luau source checkout is a separate, untracked build dependency. The tests and fixture compiler link Luau libraries; the decompiler includes Luau's bytecode headers.

Luau is distributed under the MIT license. Its license notice is retained in `docs/Luau-LICENSE.txt`. Astra's parser, dataflow analysis, source reconstruction, CLI, and test harness are implemented in this repository.

The website uses the official Luau logo from [luau-lang/site](https://github.com/luau-lang/site/blob/master/logo.svg). Its MIT notice is retained in `site/assets/luau-logo-LICENSE.txt`.

# Website fonts

The website uses Satoshi and Switzer by Indian Type Foundry. The build obtains the official Fontshare packages and copies their variable WOFF2 files into the generated website. Font binaries are cached locally in ignored artifacts and are not distributed in this source repository. The package license is retained in `site/assets/fonts/Fontshare-LICENSE.txt`; source URLs are recorded in `site/assets/fonts/SOURCES.txt`.

JetBrains Mono is bundled for code text from the [official JetBrains repository](https://github.com/JetBrains/JetBrainsMono). Its OFL-1.1 notice is retained in `site/assets/fonts/JetBrainsMono-OFL.txt`.
