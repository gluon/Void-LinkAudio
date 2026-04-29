{
	"patcher" : 	{
		"fileversion" : 1,
		"appversion" : 		{
			"major" : 9,
			"minor" : 0,
			"revision" : 0,
			"architecture" : "x64",
			"modernui" : 1
		}
,
		"classnamespace" : "box",
		"rect" : [ 80.0, 80.0, 900.0, 600.0 ],
		"bglocked" : 0,
		"openinpresentation" : 0,
		"default_fontsize" : 12.0,
		"default_fontface" : 0,
		"default_fontname" : "Arial",
		"gridonopen" : 1,
		"gridsize" : [ 15.0, 15.0 ],
		"gridsnaponopen" : 1,
		"objectsnaponopen" : 1,
		"statusbarvisible" : 2,
		"toolbarvisible" : 1,
		"lefttoolbarpinned" : 0,
		"toptoolbarpinned" : 0,
		"righttoolbarpinned" : 0,
		"bottomtoolbarpinned" : 0,
		"toolbars_unpinned_last_save" : 0,
		"tallnewobj" : 0,
		"boxanimatetime" : 200,
		"enablehscroll" : 1,
		"enablevscroll" : 1,
		"devicewidth" : 0.0,
		"description" : "",
		"digest" : "",
		"tags" : "",
		"style" : "",
		"subpatcher_template" : "",
		"assistshowspatchername" : 0,
		"boxes" : [
			{
				"box" : 				{
					"id" : "obj-1",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 30.0, 20.0, 600.0, 26.0 ],
					"text" : "void.linkaudio.in~ — receive audio from an Ableton Link Audio channel",
					"fontsize" : 16.0
				}
			}
,
			{
				"box" : 				{
					"id" : "obj-2",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 30.0, 50.0, 700.0, 22.0 ],
					"text" : "Set @channel to the exact channel name to subscribe to (e.g. 'Main' for Live's master output)."
				}
			}
,
			{
				"box" : 				{
					"id" : "obj-3",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 30.0, 70.0, 700.0, 22.0 ],
					"text" : "Subscription is automatic. Send 'info' for a status dictionary on the dumpout."
				}
			}
,
			{
				"box" : 				{
					"id" : "obj-bang",
					"maxclass" : "button",
					"numinlets" : 1,
					"numoutlets" : 1,
					"outlettype" : [ "bang" ],
					"patching_rect" : [ 30.0, 130.0, 24.0, 24.0 ]
				}
			}
,
			{
				"box" : 				{
					"id" : "obj-info",
					"maxclass" : "message",
					"numinlets" : 2,
					"numoutlets" : 1,
					"outlettype" : [ "" ],
					"patching_rect" : [ 70.0, 130.0, 40.0, 22.0 ],
					"text" : "info"
				}
			}
,
			{
				"box" : 				{
					"id" : "obj-comment-bang",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 130.0, 130.0, 240.0, 22.0 ],
					"text" : "← bang: silent retry  /  info: dump dict"
				}
			}
,
			{
				"box" : 				{
					"id" : "obj-in",
					"maxclass" : "newobj",
					"numinlets" : 1,
					"numoutlets" : 3,
					"outlettype" : [ "signal", "signal", "" ],
					"patching_rect" : [ 30.0, 180.0, 380.0, 22.0 ],
					"text" : "void.linkaudio.in~ @channel Main @poll 1 @pollinterval 250"
				}
			}
,
			{
				"box" : 				{
					"id" : "obj-meterL",
					"maxclass" : "meter~",
					"numinlets" : 1,
					"numoutlets" : 1,
					"outlettype" : [ "float" ],
					"patching_rect" : [ 30.0, 230.0, 80.0, 13.0 ]
				}
			}
,
			{
				"box" : 				{
					"id" : "obj-meterR",
					"maxclass" : "meter~",
					"numinlets" : 1,
					"numoutlets" : 1,
					"outlettype" : [ "float" ],
					"patching_rect" : [ 130.0, 230.0, 80.0, 13.0 ]
				}
			}
,
			{
				"box" : 				{
					"id" : "obj-ezdac",
					"maxclass" : "ezdac~",
					"numinlets" : 2,
					"numoutlets" : 0,
					"patching_rect" : [ 30.0, 270.0, 45.0, 45.0 ]
				}
			}
,
			{
				"box" : 				{
					"id" : "obj-dictview",
					"maxclass" : "newobj",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 450.0, 180.0, 100.0, 22.0 ],
					"text" : "dict.view"
				}
			}
,
			{
				"box" : 				{
					"id" : "obj-comment-dict",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 450.0, 155.0, 240.0, 22.0 ],
					"text" : "double-click to inspect status dict"
				}
			}
,
			{
				"box" : 				{
					"id" : "obj-comment-meters",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 230.0, 230.0, 200.0, 22.0 ],
					"text" : "L / R received audio"
				}
			}
,
			{
				"box" : 				{
					"id" : "obj-tip",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 30.0, 360.0, 800.0, 60.0 ],
					"text" : "TIP: in Live, enable Link Audio in Preferences → Link, set a track's input to receive a Link Audio peer's channel, and audio will flow either way. The status dict tells you which channels are visible on the network at any time."
				}
			}

		],
		"lines" : [
			{
				"patchline" : 				{
					"destination" : [ "obj-in", 0 ],
					"source" : [ "obj-bang", 0 ]
				}

			}
,
			{
				"patchline" : 				{
					"destination" : [ "obj-in", 0 ],
					"source" : [ "obj-info", 0 ]
				}

			}
,
			{
				"patchline" : 				{
					"destination" : [ "obj-meterL", 0 ],
					"source" : [ "obj-in", 0 ]
				}

			}
,
			{
				"patchline" : 				{
					"destination" : [ "obj-meterR", 0 ],
					"source" : [ "obj-in", 1 ]
				}

			}
,
			{
				"patchline" : 				{
					"destination" : [ "obj-ezdac", 0 ],
					"source" : [ "obj-in", 0 ]
				}

			}
,
			{
				"patchline" : 				{
					"destination" : [ "obj-ezdac", 1 ],
					"source" : [ "obj-in", 1 ]
				}

			}
,
			{
				"patchline" : 				{
					"destination" : [ "obj-dictview", 0 ],
					"source" : [ "obj-in", 2 ]
				}

			}

		]
	}

}
