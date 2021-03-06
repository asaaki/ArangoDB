/*jslint indent: 2, nomen: true, maxlen: 100, vars: true, white: true, plusplus: true */
/*global Backbone, $, window, EJS, arangoHelper, _, templateEngine*/

(function() {
  "use strict";
  var checkMount = function(mount) {
    /*jslint regexp: true*/
    var regex = /^(\/[^\/\s]+)+$/;
    /*jslint regexp: false*/
    if (!regex.test(mount)){
      arangoHelper.arangoError("Please give a valid mount point, e.g.: /myPath");
      return false;
    }
    return true;
  };

  window.FoxxActiveView = Backbone.View.extend({
    tagName: 'div',
    className: "tile",
    template: templateEngine.createTemplate("foxxActiveView.ejs"),

    events: {
      'click .icon_arangodb_info' : 'showDocu',
      'click .icon_arangodb_settings2' : 'editFoxxDialog',
      'click .icon' : 'openAppInNewTab'
    },

    initialize: function(){
      this._show = true;
      this.buttonConfig = [
        window.modalView.createDeleteButton(
          "Uninstall", this.uninstall.bind(this)
        ),
        window.modalView.createSuccessButton(
          "Save", this.changeFoxx.bind(this)
        )
      ];
      this.showMod = window.modalView.show.bind(
        window.modalView,
        "modalTable.ejs",
        "Modify Application"
      );
      this.appsView = this.options.appsView;
      _.bindAll(this, 'render');
    },

    toggle: function(type, shouldShow) {
      if (this.model.get("development")) {
        if ("devel" === type) {
          this._show = shouldShow;
        }
      } else {
        if ("active" === type && true === this.model.get("active")) {
          this._show = shouldShow;
        }
        if ("inactive" === type && false === this.model.get("active")) {
          this._show = shouldShow;
        }
      }
      if (this._show) {
        $(this.el).show();
      } else {
        $(this.el).hide();
      }
    },

    fillValues: function() {
      var list = [],
        appInfos = this.model.get("app").split(":"),
        name = appInfos[1],
//        versOptions = [],
        isSystem,
        active,
        modView = window.modalView;
      if (this.model.get("isSystem")) {
        isSystem = "Yes";
      } else {
        isSystem = "No";
      }
      if (this.model.get("active")) {
        active = "Active";
      } else {
        active = "Inactive";
      }
      list.push(modView.createReadOnlyEntry(
        "id_name", "Name", name
      ));
      list.push(modView.createTextEntry(
        "change-mount-point", "Mount", this.model.get("mount"),
        "The path where the app can be reached.",
        "mount-path",
        true
      ));
      /*
       * For the future, update apps to available newer versions
       * versOptions.push(modView.createOptionEntry(appInfos[2]));
       * list.push(modView.createSelectEntry()
       */
      list.push(modView.createReadOnlyEntry(
        "id_version", "Version", appInfos[2]
      ));
      list.push(modView.createReadOnlyEntry(
        "id_system", "System", isSystem
      ));
      list.push(modView.createReadOnlyEntry(
        "id_status", "Status", active
      ));
      return list;
    },

    editFoxxDialog: function(event) {
      event.stopPropagation();
      if (this.model.get("isSystem") || this.model.get("development")) {
        this.buttonConfig[0].disabled = true;
      } else {
        delete this.buttonConfig[0].disabled;
      }
      this.showMod(this.buttonConfig, this.fillValues());
    },

    changeFoxx: function() {
      var mount = $("#change-mount-point").val();
      var failed = false;
      var app = this.model.get("app");
      var prefix = this.model.get("options").collectionPrefix;
      if (mount !== this.model.get("mount")) {
        if (checkMount(mount)) {
          $.ajax("foxx/move/" + this.model.get("_key"), {
            async: false,
            type: "PUT",
            data: JSON.stringify({
              mount: mount,
              app: app,
              prefix: prefix
            }),
            dataType: "json",
            error: function(data) {
              failed = true;
              arangoHelper.arangoError(data.responseText);
            }
          });
        } else {
          return;
        }
      }
      // TO_DO change version
      if (!failed) {
        window.modalView.hide();
        this.appsView.reload();
      }
    },

    openAppInNewTab: function() {
      var url = window.location.origin + "/_db/_system" + this.model.get("mount");
      var windowRef = window.open(url, this.model.get("title"));
      windowRef.focus();
    },

    uninstall: function () {
      if (!this.model.get("isSystem")) {
        this.model.destroy({ async: false });
        window.modalView.hide();
        this.appsView.reload();
      }
    },

    showDocu: function(event) {
      event.stopPropagation();
      window.App.navigate(
        "application/documentation/" + encodeURIComponent(this.model.get("_key")),
        {
          trigger: true
        }
      );
    },

    render: function(){
      if (this._show) {
        $(this.el).html(this.template.render(this.model));
      }

      return $(this.el);
    }
  });
}());
