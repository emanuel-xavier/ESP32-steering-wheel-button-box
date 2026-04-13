package main

import (
	"context"
	"embed"

	"github.com/wailsapp/wails/v2"
	"github.com/wailsapp/wails/v2/pkg/options"
	"github.com/wailsapp/wails/v2/pkg/options/assetserver"
	"github.com/wailsapp/wails/v2/pkg/runtime"
)

//go:embed all:frontend
var assets embed.FS

func main() {
	app := NewApp()
	err := wails.Run(&options.App{
		Title:     "ButtonBox Config",
		Width:     540,
		Height:    860,
		MinWidth:  400,
		MinHeight: 500,
		AssetServer: &assetserver.Options{
			Assets: assets,
		},
		OnStartup: func(ctx context.Context) {
			app.startup(ctx)
			runtime.WindowCenter(ctx)
		},
		Bind:             []interface{}{app},
		BackgroundColour: &options.RGBA{R: 15, G: 17, B: 23, A: 255},
	})
	if err != nil {
		println("Error:", err.Error())
	}
}
