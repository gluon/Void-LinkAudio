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
					"text" : "void.linkaudio.out~ — publish audio to an Ableton Link Audio channel",
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
					"text" : "@stereo 1 → two signal inlets (L, R).  @stereo 0 → one inlet (mono).  Set as a creation argument."
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
					"text" : "Set @channel to the name your stream will be advertised under (other peers see this name)."
				}
			}
,
			{
				"box" : 				{
					"id" : "obj-cycleL",
					"maxclass" : "newobj",
					"numinlets" : 1,
					"numoutlets" : 1,
					"outlettype" : [ "signal" ],
					"patching_rect" : [ 30.0, 130.0, 80.0, 22.0 ],
					"text" : "cycle~ 220"
				}
			}
,
			{
				"box" : 				{
					"id" : "obj-cycleR",
					"maxclass" : "newobj",
					"numinlets" : 1,
					"numoutlets" : 1,
					"outlettype" : [ "signal" ],
					"patching_rect" : [ 130.0, 130.0, 80.0, 22.0 ],
					"text" : "cycle~ 330"
				}
			}
,
			{
				"box" : 				{
					"id" : "obj-mulL",
					"maxclass" : "newobj",
					"numinlets" : 2,
					"numoutlets" : 1,
					"outlettype" : [ "signal" ],
					"patching_rect" : [ 30.0, 165.0, 50.0, 22.0 ],
					"text" : "*~ 0.2"
				}
			}
,
			{
				"box" : 				{
					"id" : "obj-mulR",
					"maxclass" : "newobj",
					"numinlets" : 2,
					"numoutlets" : 1,
					"outlettype" : [ "signal" ],
					"patching_rect" : [ 130.0, 165.0, 50.0, 22.0 ],
					"text" : "*~ 0.2"
				}
			}
,
			{
				"box" : 				{
					"id" : "obj-out",
					"maxclass" : "newobj",
					"numinlets" : 2,
					"numoutlets" : 1,
					"outlettype" : [ "" ],
					"patching_rect" : [ 30.0, 220.0, 380.0, 22.0 ],
					"text" : "void.linkaudio.out~ @stereo 1 @channel \"Max Out\""
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
					"patching_rect" : [ 450.0, 220.0, 24.0, 24.0 ]
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
					"patching_rect" : [ 490.0, 220.0, 40.0, 22.0 ],
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
					"patching_rect" : [ 540.0, 220.0, 240.0, 22.0 ],
					"text" : "← bang: silent retry  /  info: dump dict"
				}
			}
,
			{
				"box" : 				{
					"id" : "obj-dictview",
					"maxclass" : "newobj",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 30.0, 270.0, 100.0, 22.0 ],
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
					"patching_rect" : [ 145.0, 270.0, 320.0, 22.0 ],
					"text" : "double-click to inspect status dict"
				}
			}
,
			{
				"box" : 				{
					"id" : "obj-tip",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 30.0, 350.0, 800.0, 80.0 ],
					"text" : "TIP: turn on the DSP. In Live, with Link Audio enabled in Preferences → Link, set a track input to 'Max / Max Out'. You should now hear this object's signal in Live. Same workflow with TouchDesigner's Link Audio In CHOP and any other Link Audio host."
				}
			}

		],
		"lines" : [
			{
				"patchline" : 				{
					"destination" : [ "obj-mulL", 0 ],
					"source" : [ "obj-cycleL", 0 ]
				}

			}
,
			{
				"patchline" : 				{
					"destination" : [ "obj-mulR", 0 ],
					"source" : [ "obj-cycleR", 0 ]
				}

			}
,
			{
				"patchline" : 				{
					"destination" : [ "obj-out", 0 ],
					"source" : [ "obj-mulL", 0 ]
				}

			}
,
			{
				"patchline" : 				{
					"destination" : [ "obj-out", 1 ],
					"source" : [ "obj-mulR", 0 ]
				}

			}
,
			{
				"patchline" : 				{
					"destination" : [ "obj-out", 0 ],
					"source" : [ "obj-bang", 0 ]
				}

			}
,
			{
				"patchline" : 				{
					"destination" : [ "obj-out", 0 ],
					"source" : [ "obj-info", 0 ]
				}

			}
,
			{
				"patchline" : 				{
					"destination" : [ "obj-dictview", 0 ],
					"source" : [ "obj-out", 0 ]
				}

			}

		]
	}

}
